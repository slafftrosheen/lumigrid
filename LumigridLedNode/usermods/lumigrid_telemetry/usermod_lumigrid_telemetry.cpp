// LumiGrid Telemetry Usermod
// Sends a periodic heartbeat HTTP GET to a configurable monitoring endpoint,
// and triggers a daily auto-reboot at a configurable time to prevent heap
// fragmentation on long-running sign installations.
//
// Heartbeat URL: GET <heartbeat_url>?mac=<MAC>&v=<firmware_version>
// Daily reboot: triggers at the configured HH:MM (requires NTP sync).

#include "wled.h"
#include <HTTPClient.h>

class UsermodLumigridTelemetry : public Usermod {
  private:
    unsigned long lastHeartbeat     = 0;
    String        heartbeatUrl      = "http://api.lumigrid.io/ping";
    String        rebootTimeStr     = "03:00";
    uint16_t      rebootMinuteOfDay = 180; // 03:00
    bool          rebootDoneToday   = false;
    uint8_t       lastRebootDay     = 255;
    static const char _name[];

    // Parse "HH:MM" → minute-of-day; returns 0 on bad input.
    static uint16_t parseHHMM(const String& s) {
      int h = 0, m = 0;
      if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return 0;
      if (h < 0 || h > 23 || m < 0 || m > 59) return 0;
      return (uint16_t)(h * 60 + m);
    }

#ifdef ARDUINO_ARCH_ESP32
    // FreeRTOS task: performs the HTTP GET and deletes itself.
    // The URL string is heap-allocated by the caller and owned by this task.
    static void heartbeatTask(void* parameter) {
      String* urlPtr = static_cast<String*>(parameter);
      if (urlPtr) {
        HTTPClient http;
        http.begin(*urlPtr);
        http.setTimeout(8000); // 8 s timeout — prevents the task from hanging
        int code = http.GET();
        http.end();
        DEBUG_PRINTF_P(PSTR("Telemetry: heartbeat → %d\n"), code);
        delete urlPtr;
      }
      vTaskDelete(NULL);
    }
#endif

    void sendHeartbeat() {
      if (heartbeatUrl.isEmpty() || !WLED_CONNECTED) return;

      // Build URL on the heap; ownership transferred to the FreeRTOS task (ESP32)
      // or released in the synchronous path (ESP8266).
      String* url = new (std::nothrow) String(heartbeatUrl);
      if (!url) return; // out of memory

      *url += "?mac=";
      *url += escapedMac;
      *url += "&v=";
      *url += VERSION;

#ifdef ARDUINO_ARCH_ESP32
      BaseType_t created = xTaskCreate(
        heartbeatTask,
        "lgtm_hb",   // short name to save RTOS name-table space
        4096,
        static_cast<void*>(url),
        1,           // low priority — must not starve LED pipeline
        NULL
      );
      if (created != pdPASS) {
        // Task creation failed — must free the URL ourselves to avoid leak
        DEBUG_PRINTLN(F("Telemetry: xTaskCreate failed, dropping heartbeat"));
        delete url;
      }
#else
      HTTPClient http;
      http.begin(*url);
      int code = http.GET();
      http.end();
      DEBUG_PRINTF_P(PSTR("Telemetry: heartbeat → %d\n"), code);
      delete url;
#endif
    }

  public:
    void setup() override {
      rebootMinuteOfDay = parseHHMM(rebootTimeStr);
    }

    void loop() override {
      // ── Heartbeat every 5 minutes ─────────────────────────────────────────
      if (millis() - lastHeartbeat > 300000UL) {
        lastHeartbeat = millis();
        sendHeartbeat();
      }

      // ── Daily auto-reboot ─────────────────────────────────────────────────
      // Requires NTP sync (localTime > 0) — WiFi alone is insufficient for
      // time-based decisions since localTime stays 0 until first NTP packet.
      if (!ntpConnected || localTime == 0) return;

      uint8_t d = day(localTime);
      if (d != lastRebootDay) {
        // New calendar day: reset the done flag
        rebootDoneToday = false;
        lastRebootDay   = d;
      }

      if (!rebootDoneToday) {
        uint16_t now_minute = (uint16_t)(hour(localTime) * 60 + minute(localTime));
        if (now_minute == rebootMinuteOfDay) {
          DEBUG_PRINTLN(F("Telemetry: triggering daily auto-reboot"));
          rebootDoneToday = true;
          doReboot = true;
        }
      }
    }

    void addToJsonInfo(JsonObject& root) override {
      JsonObject user = root.containsKey("u") ? root["u"].as<JsonObject>() : root.createNestedObject("u");
      JsonObject info = user.createNestedObject(FPSTR(_name));
      info["rebootTime"] = rebootTimeStr;
      info["ntpOk"]      = (localTime > 0);
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top["heartbeat_url"] = heartbeatUrl;
      top["reboot_time"]   = rebootTimeStr;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) return false;

      String newUrl = heartbeatUrl;
      if (getJsonValue(top["heartbeat_url"], newUrl)) heartbeatUrl = newUrl;

      String newTime = rebootTimeStr;
      if (getJsonValue(top["reboot_time"], newTime) && newTime != rebootTimeStr) {
        rebootTimeStr     = newTime;
        rebootMinuteOfDay = parseHHMM(rebootTimeStr);
      }
      return true;
    }

    uint16_t getId() override {
      return USERMOD_ID_LUMIGRID_TELEMETRY;
    }
};

const char UsermodLumigridTelemetry::_name[] PROGMEM = "Lumigrid_Telemetry";
static UsermodLumigridTelemetry lumigridTelemetry;
REGISTER_USERMOD(lumigridTelemetry);
