// LumiGrid Content Scheduler Usermod
// Plays animations from a time-based schedule stored in /animations.json.
// Requires NTP time to be synced (localTime > 0) before scheduling is active.
//
// Schedule file format (/animations.json):
// {
//   "animations": [
//     {
//       "id": "morning",
//       "file": "greeting.gif",
//       "start_time": "06:00",
//       "duration_minutes": 120,
//       "fallback_preset": 1
//     }
//   ]
// }

#include "wled.h"
#include <vector>

struct AnimationBlock {
  char     id[32];
  char     file[32];
  uint16_t start_minute;
  uint16_t duration_minutes;
  uint8_t  fallback_preset;
};

class UsermodLumigridContentScheduler : public Usermod {
  private:
    std::vector<AnimationBlock> animations;
    unsigned long lastCheck       = 0;
    bool          currentlyPlaying = false;
    uint8_t       activeFallback   = 0;
    bool          enabled          = true;
    static const char _name[];

    void loadSchedule() {
      animations.clear();
      if (!WLED_FS.exists("/animations.json")) {
        DEBUG_PRINTLN(F("Scheduler: /animations.json not found."));
        return;
      }

      File f = WLED_FS.open("/animations.json", "r");
      if (!f) return;

      // Use heap-allocated doc to avoid large stack allocation
      DynamicJsonDocument* doc = new DynamicJsonDocument(2048);
      DeserializationError err = deserializeJson(*doc, f);
      f.close();

      if (err) {
        DEBUG_PRINTLN(F("Scheduler: Failed to parse /animations.json"));
        delete doc;
        return;
      }

      JsonArray anims = (*doc)["animations"];
      for (JsonObject anim : anims) {
        AnimationBlock block;
        strlcpy(block.id,   anim["id"]   | "", sizeof(block.id));
        strlcpy(block.file, anim["file"] | "", sizeof(block.file));

        const char* timeStr = anim["start_time"] | "00:00";
        int h = 0, m = 0;
        sscanf(timeStr, "%d:%d", &h, &m);
        block.start_minute      = (uint16_t)(h * 60 + m);
        block.duration_minutes  = anim["duration_minutes"] | 0;
        block.fallback_preset   = anim["fallback_preset"]  | 1;
        animations.push_back(block);
      }
      delete doc;
      DEBUG_PRINTF_P(PSTR("Scheduler: loaded %u blocks\n"), (unsigned)animations.size());
    }

  public:
    void setup() override {
      loadSchedule();
    }

    void loop() override {
      if (!enabled) return;
      if (millis() - lastCheck < 5000) return;
      lastCheck = millis();

      // Require NTP time to be synced — localTime == 0 means not yet received
      if (!WLED_CONNECTED || localTime == 0) return;

      uint16_t current_minute = (uint16_t)(hour(localTime) * 60 + minute(localTime));

      bool foundActive = false;
      for (const auto& block : animations) {
        uint16_t end_minute = block.start_minute + block.duration_minutes;
        if (current_minute >= block.start_minute && current_minute < end_minute) {
          foundActive = true;
          if (!currentlyPlaying) {
            DEBUG_PRINTF_P(PSTR("Scheduler: starting \"%s\"\n"), block.file);
            activeFallback  = block.fallback_preset;
            currentlyPlaying = true;

            // Set the segment name safely through the strip API
            Segment& seg = strip.getSegment(0);
            seg.setName(block.file); // WLED Segment::setName() manages the buffer safely

            seg.setMode(FX_MODE_IMAGE, true);
            colorUpdated(CALL_MODE_DIRECT_CHANGE);
          }
          break; // first matching block wins
        }
      }

      if (!foundActive && currentlyPlaying) {
        DEBUG_PRINTLN(F("Scheduler: block ended — restoring fallback preset"));
        currentlyPlaying = false;
        applyPreset(activeFallback, CALL_MODE_DIRECT_CHANGE);
      }
    }

    // Expose reload trigger via JSON state (POST /json/state {"Lumigrid_ContentScheduler":{"reload":true}})
    void readFromJsonState(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) return;
      bool reload = false;
      if (getJsonValue(top["reload"], reload) && reload) {
        loadSchedule();
        currentlyPlaying = false;
        DEBUG_PRINTLN(F("Scheduler: schedule reloaded via JSON state"));
      }
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top["enabled"] = enabled;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) return false;
      getJsonValue(top["enabled"], enabled);
      return true;
    }

    void addToJsonInfo(JsonObject& root) override {
      if (!enabled) return;
      JsonObject user = root.containsKey("u") ? root["u"].as<JsonObject>() : root.createNestedObject("u");
      JsonObject info = user.createNestedObject(FPSTR(_name));
      info["blocks"]  = (int)animations.size();
      info["playing"] = currentlyPlaying;
      info["ntpOk"]   = (localTime > 0);
    }

    uint16_t getId() override {
      return USERMOD_ID_LUMIGRID_CONTENT_SCHED;
    }
};

const char UsermodLumigridContentScheduler::_name[] PROGMEM = "Lumigrid_ContentScheduler";
static UsermodLumigridContentScheduler lumigridContentScheduler;
REGISTER_USERMOD(lumigridContentScheduler);
