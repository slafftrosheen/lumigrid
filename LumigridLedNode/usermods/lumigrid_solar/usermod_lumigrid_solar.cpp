// LumiGrid Solar Schedule Usermod
// Automatically adjusts WLED brightness and applies presets at
// sunrise and sunset, computed daily from GPS coordinates.
//
// Algorithm: Jean Meeus "Astronomical Algorithms" Ch. 15 (simplified NOAA variant)
//
// Config keys (in cfg.json under "um.Lumigrid_Solar"):
//   latitude     float  — decimal degrees, N positive  (e.g. 52.37)
//   longitude    float  — decimal degrees, E positive  (e.g. 4.90)
//   rampMinutes  uint   — brightness ramp duration in minutes (1-120, default 30)
//   presetDawn   uint   — WLED preset to apply at start of dawn ramp
//   presetDay    uint   — WLED preset to apply at end of dawn ramp (solar noon)
//   presetDusk   uint   — WLED preset to apply at start of dusk ramp
//   presetNight  uint   — WLED preset to apply at end of dusk ramp
//   enabled      bool

#include "wled.h"
#include <math.h>

#ifndef DEG_TO_RAD
  #define DEG_TO_RAD 0.017453292519943295
#endif
#ifndef RAD_TO_DEG
  #define RAD_TO_DEG 57.29577951308232
#endif

class UsermodLumigridSolar : public Usermod {
  private:
    // ── Config ─────────────────────────────────────────────────────────────────
    float   latitude     = 52.37f;   // Amsterdam default
    float   longitude    = 4.90f;
    uint8_t rampMinutes  = 30;
    uint8_t presetDawn   = 1;
    uint8_t presetDay    = 2;
    uint8_t presetDusk   = 3;
    uint8_t presetNight  = 4;
    bool    enabled      = true;

    static const char _name[];

    // ── Runtime state ──────────────────────────────────────────────────────────
    int16_t sunriseMin = -1;  // minutes since midnight, local time (-1 = not computed)
    int16_t sunsetMin  = -1;
    int16_t lastCalcDay = -1; // day-of-year when times were computed

    // Current phase: 0=night, 1=dawn ramp, 2=day, 3=dusk ramp
    uint8_t phase = 0;

    // ── Jean Meeus sunrise/sunset algorithm ───────────────────────────────────
    // Returns time in minutes since midnight (local), or -1 if sun never rises/sets.
    // rise=true for sunrise, rise=false for sunset.
    int16_t calcSunTime(bool rise) {
      if (localTime == 0) return -1;

      // Julian Day Number from UNIX timestamp
      double JD = (double)localTime / 86400.0 + 2440587.5;

      // Julian century
      double T = (JD - 2451545.0) / 36525.0;

      // Geometric mean longitude of the sun (degrees)
      double L0 = fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);

      // Geometric mean anomaly of the sun (degrees)
      double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
      double Mrad = M * DEG_TO_RAD;

      // Equation of centre
      double C = (1.914602 - T * (0.004817 + 0.000014 * T)) * sin(Mrad)
               + (0.019993 - 0.000101 * T) * sin(2 * Mrad)
               + 0.000289 * sin(3 * Mrad);

      // Sun's true longitude
      double sunLon = L0 + C;

      // Apparent longitude
      double omega = 125.04 - 1934.136 * T;
      double lambda = (sunLon - 0.00569 - 0.00478 * sin(omega * DEG_TO_RAD)) * DEG_TO_RAD;

      // Mean obliquity of the ecliptic
      double eps0 = 23.0 + (26.0 + (21.448 - T * (46.8150 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
      double eps  = (eps0 + 0.00256 * cos(omega * DEG_TO_RAD)) * DEG_TO_RAD;

      // Sun declination
      double decl = asin(sin(eps) * sin(lambda));

      // Hour angle at horizon (zenith = 90.833° for atmospheric refraction)
      double cosH = (cos(90.833 * DEG_TO_RAD) - sin(latitude * DEG_TO_RAD) * sin(decl))
                  / (cos(latitude * DEG_TO_RAD) * cos(decl));

      if (cosH < -1.0 || cosH > 1.0) return -1; // midnight sun or polar night

      double H = acos(cosH) * RAD_TO_DEG; // 0-180

      // Solar noon: Julian day of solar noon
      double Jnoon = 2451545.0 + 0.0009 + (((-longitude) / 360.0) - (int)((-longitude) / 360.0)) - T / 2.0;
      // Correction (iterative approximation skipped — NOAA single-pass sufficient)
      double Jx = rise ? (Jnoon - H / 360.0) : (Jnoon + H / 360.0);

      // Convert Julian day back to minutes since midnight local time
      double unixSec = (Jx - 2440587.5) * 86400.0;

      // Apply UTC offset (WLED stores timezone in utcOffsetSecs)
      double localSec = unixSec + (double)utcOffsetSecs;
      int32_t mins = (int32_t)(fmod(localSec, 86400.0) / 60.0);
      if (mins < 0) mins += 1440;
      return (int16_t)(mins % 1440);
    }

    void recomputeIfNeeded() {
      if (localTime == 0) return;
      int16_t doy = (int16_t)(day(localTime) + (month(localTime) - 1) * 30);
      if (doy == lastCalcDay) return;
      lastCalcDay = doy;
      sunriseMin  = calcSunTime(true);
      sunsetMin   = calcSunTime(false);
      DEBUG_PRINTF_P(PSTR("Solar: sunrise=%d sunset=%d (local mins)\n"),
                     (int)sunriseMin, (int)sunsetMin);
    }

    // Apply a WLED preset safely
    static void safeApplyPreset(uint8_t ps) {
      if (ps == 0) return;
      applyPreset(ps, CALL_MODE_DIRECT_CHANGE);
    }

    // Ramp brightness toward target over time (called each loop, returns true when done)
    bool rampBri(uint8_t target, uint8_t &step) {
      if (bri == target) return true;
      if (bri < target) bri = min((uint8_t)(bri + step), target);
      else              bri = max((uint8_t)(bri - step), target);
      colorUpdated(CALL_MODE_DIRECT_CHANGE);
      return bri == target;
    }

    // Current minute of day
    static int16_t nowMinutes() {
      return (int16_t)(hour(localTime) * 60 + minute(localTime));
    }

  public:
    void setup() override { /* times computed on first loop tick */ }

    void loop() override {
      if (!enabled) return;
      if (!ntpConnected || localTime == 0) return;

      // Recompute sunrise/sunset once per day
      recomputeIfNeeded();
      if (sunriseMin < 0 || sunsetMin < 0) return; // polar conditions

      // Rate-limit to 1 evaluation per second
      static uint32_t lastEval = 0;
      if (millis() - lastEval < 1000) return;
      lastEval = millis();

      int16_t now     = nowMinutes();
      int16_t dawnEnd = sunriseMin;
      int16_t dawnStart = sunriseMin - rampMinutes;
      int16_t duskStart = sunsetMin;
      int16_t duskEnd   = sunsetMin + rampMinutes;

      // Clamp to 0-1439
      if (dawnStart < 0) dawnStart = 0;
      if (duskEnd > 1439) duskEnd = 1439;

      uint8_t newPhase = phase;

      if (now >= dawnStart && now < dawnEnd) {
        newPhase = 1; // Dawn ramp
      } else if (now >= dawnEnd && now < duskStart) {
        newPhase = 2; // Day
      } else if (now >= duskStart && now < duskEnd) {
        newPhase = 3; // Dusk ramp
      } else {
        newPhase = 0; // Night
      }

      // Phase entry actions
      if (newPhase != phase) {
        phase = newPhase;
        switch (phase) {
          case 0: safeApplyPreset(presetNight); bri = 0; colorUpdated(CALL_MODE_DIRECT_CHANGE); break;
          case 1: safeApplyPreset(presetDawn);  bri = 0; colorUpdated(CALL_MODE_DIRECT_CHANGE); break;
          case 2: safeApplyPreset(presetDay);   bri = 255; colorUpdated(CALL_MODE_DIRECT_CHANGE); break;
          case 3: safeApplyPreset(presetDusk);  bri = 255; colorUpdated(CALL_MODE_DIRECT_CHANGE); break;
        }
        DEBUG_PRINTF_P(PSTR("Solar: phase → %u at min=%d\n"), phase, (int)now);
      }

      // Smooth ramp during dawn/dusk (advance ~1/rampMin per minute)
      if (phase == 1 || phase == 3) {
        int16_t elapsed = (phase == 1) ? (now - dawnStart) : (now - duskStart);
        uint8_t pct = (uint8_t)((int32_t)elapsed * 255 / max((int16_t)1, (int16_t)rampMinutes));
        uint8_t target = (phase == 1) ? pct : (255 - pct);
        if (bri != target) {
          bri = target;
          colorUpdated(CALL_MODE_DIRECT_CHANGE);
        }
      }
    }

    void addToJsonInfo(JsonObject& root) override {
      if (!enabled) return;
      JsonObject user = root.containsKey("u") ? root["u"].as<JsonObject>() : root.createNestedObject("u");
      JsonObject s    = user.createNestedObject(FPSTR(_name));
      s["sunrise_min"] = sunriseMin;
      s["sunset_min"]  = sunsetMin;
      s["phase"]       = phase;
      s["ntpOk"]       = (localTime > 0);
      // Format times as HH:MM for readability
      if (sunriseMin >= 0) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", sunriseMin / 60, sunriseMin % 60);
        s["sunrise"] = buf;
      }
      if (sunsetMin >= 0) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", sunsetMin / 60, sunsetMin % 60);
        s["sunset"] = buf;
      }
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top["enabled"]     = enabled;
      top["latitude"]    = latitude;
      top["longitude"]   = longitude;
      top["rampMinutes"] = rampMinutes;
      top["presetDawn"]  = presetDawn;
      top["presetDay"]   = presetDay;
      top["presetDusk"]  = presetDusk;
      top["presetNight"] = presetNight;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) return false;

      bool changed = false;
      float  newLat = latitude,  newLon = longitude;
      getJsonValue(top["enabled"],     enabled);
      if (getJsonValue(top["latitude"],    newLat) && newLat != latitude)  { latitude = newLat;  changed = true; }
      if (getJsonValue(top["longitude"],   newLon) && newLon != longitude) { longitude = newLon; changed = true; }
      getJsonValue(top["rampMinutes"], rampMinutes);
      getJsonValue(top["presetDawn"],  presetDawn);
      getJsonValue(top["presetDay"],   presetDay);
      getJsonValue(top["presetDusk"],  presetDusk);
      getJsonValue(top["presetNight"], presetNight);

      // Force re-computation if coordinates changed
      if (changed) lastCalcDay = -1;
      return true;
    }

    uint16_t getId() override {
      return USERMOD_ID_LUMIGRID_SOLAR;
    }
};

const char UsermodLumigridSolar::_name[] PROGMEM = "Lumigrid_Solar";
static UsermodLumigridSolar lumigridSolar;
REGISTER_USERMOD(lumigridSolar);
