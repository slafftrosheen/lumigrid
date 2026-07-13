// LumiGrid PCA9685 PWM Usermod
// Drives 8 PWM channels (LED0–LED7) on a single PCA9685 at I2C address 0x40.
// Board: ESP32-WROOM-32U
// I2C: SDA=GPIO21, SCL=GPIO22
// OE#:  GPIO25 (active-low output enable, pulled LOW in setup)
//
// Channel types:
//   0 = Off
//   1 = Dumb PWM  (1 PCA channel, brightness = max of RGBW components)
//   2 = CCT       (2 PCA channels: WW + CW)
//   3 = RGB        (3 PCA channels)
//   4 = RGBW       (4 PCA channels)

#pragma once

#include "wled.h"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class UsermodLumigridPCA9685 : public Usermod {
  private:
    // ── Hardware constants ────────────────────────────────────────────────────
    static constexpr uint8_t  NUM_CHANNELS = 8;     // LED0–LED7
    static constexpr uint8_t  PCA_ADDR     = 0x40;  // single chip
    static constexpr uint16_t PWM_FREQ_DEFAULT = 1000; // Hz — safe for LED dimming

    // ── State ─────────────────────────────────────────────────────────────────
    bool    enabled   = true;
    bool    initDone  = false;
    bool    pcaFound  = false;

    // ── Channel configuration ─────────────────────────────────────────────────
    uint8_t channelType[NUM_CHANNELS];          // 0–4 per channel
    char    channelLabel[NUM_CHANNELS][24];     // user-visible name per channel
    uint8_t channelCalibration[NUM_CHANNELS];   // 0–100 % brightness cap per channel

    // ── Emergency button ──────────────────────────────────────────────────────
    int8_t  emergencyBtnPin = 32;
    bool    lastBtnState    = HIGH;
    unsigned long btnPressTime = 0;
    bool    emergencyOff    = false;

    // ── Temperature sensor (DS18B20) ─────────────────────────────────────────
    // Default -1 = disabled. Do NOT use a pin that is shared with an addressable
    // LED channel (GPIO 4/17/18/19/23/26/27). A free GPIO such as GPIO33 is safe.
    int8_t  tempPin         = -1;
    float   currentTemp     = 0.0f;
    unsigned long lastTempUpdate = 0;
    bool    tempSensorFound = false;
    OneWire*           oneWire = nullptr;
    DallasTemperature* sensors = nullptr;

    // ── PWM ──────────────────────────────────────────────────────────────────
    uint16_t pwmFrequency = PWM_FREQ_DEFAULT; // 24–1526 Hz
    Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PCA_ADDR);

    // ── Timing ────────────────────────────────────────────────────────────────
    uint32_t      lastUpdate   = 0;
    unsigned long lastWatchdog = 0;

    static const char _name[];

    // ─────────────────────────────────────────────────────────────────────────
    // Private helpers
    // ─────────────────────────────────────────────────────────────────────────

    void setupTemperature() {
      if (tempPin < 0) {
        tempSensorFound = false;
        return;
      }
      delete sensors; sensors = nullptr;
      delete oneWire; oneWire = nullptr;
      oneWire = new OneWire(tempPin);
      sensors = new DallasTemperature(oneWire);
      sensors->begin();
      tempSensorFound = (sensors->getDeviceCount() > 0);
      DEBUG_PRINTF_P(PSTR("LumiGrid PCA9685: temp sensor %s (pin %d)\n"),
                     tempSensorFound ? "found" : "not found", (int)tempPin);
    }

    // Map 8-bit color value → 12-bit PCA9685 duty cycle.
    //   ch  – channel index (0–7), used for per-channel calibration lookup
    // Applies: per-channel calibration cap → global bri → thermal throttle → quadratic gamma.
    uint16_t mapTo12Bit(uint8_t val, uint8_t ch) {
      if (emergencyOff || !enabled) return 0;

      // Thermal protection thresholds
      float mult = 1.0f;
      if (currentTemp > 60.0f) mult = 0.5f;
      if (currentTemp > 75.0f) mult = 0.1f;
      if (currentTemp > 85.0f) return 0; // full shutdown

      // Per-channel calibration cap (0–100 %)
      uint8_t capped = (uint16_t(val) * uint16_t(channelCalibration[ch])) / 100u;

      // Scale by global master brightness and thermal multiplier
      uint32_t bri32  = uint32_t(float(bri) * mult);
      uint16_t linear = uint16_t((uint32_t(capped) * bri32 * 4095u) / 65025u);

      // Quadratic gamma (~2.0) for perceptually linear dimming
      return uint16_t((uint32_t(linear) * uint32_t(linear)) / 4095u);
    }

    // Write a 12-bit value to a single PCA9685 channel (bounds-checked).
    void applyPWM(uint8_t ch, uint16_t val) {
      if (!pcaFound || ch >= NUM_CHANNELS) return;
      pwm.setPin(ch, val, false);
    }

  public:
    UsermodLumigridPCA9685() {
      for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        channelType[i]        = 1;   // Dumb PWM
        channelCalibration[i] = 100; // full range
        snprintf(channelLabel[i], sizeof(channelLabel[i]), "LED%d", i);
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // WLED lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    void setup() override {
      // Enable PCA9685 output (OE# = active-low)
      #ifdef PCA9685_OE
        if (PinManager::allocatePin(PCA9685_OE, true, PinOwner::UM_Unspecified)) {
          pinMode(PCA9685_OE, OUTPUT);
          digitalWrite(PCA9685_OE, LOW);
        }
      #else
        if (PinManager::allocatePin(25, true, PinOwner::UM_Unspecified)) {
          pinMode(25, OUTPUT);     // default OE# pin
          digitalWrite(25, LOW);
        }
      #endif

      #if defined(I2CSDAPIN) && defined(I2CSCLPIN)
        Wire.begin(I2CSDAPIN, I2CSCLPIN);
      #else
        Wire.begin(); 
      #endif

      // Probe single PCA9685 chip
      Wire.beginTransmission(PCA_ADDR);
      pcaFound = (Wire.endTransmission() == 0);
      if (pcaFound) {
        pwm.begin();
        pwm.setPWMFreq(pwmFrequency);
        DEBUG_PRINTF_P(PSTR("LumiGrid PCA9685: chip found at 0x%02X, %d Hz\n"),
                       (int)PCA_ADDR, (int)pwmFrequency);
      } else {
        DEBUG_PRINTLN(F("LumiGrid PCA9685: no chip at 0x40!"));
      }

      // Emergency button (active-low, internal pull-up)
      if (emergencyBtnPin >= 0) {
        pinMode(emergencyBtnPin, INPUT_PULLUP);
      }

      setupTemperature();
      initDone = true;
    }

    void loop() override {
      if (!enabled || !initDone || strip.isUpdating()) return;

      // ── I2C watchdog: re-init if chip lost ──────────────────────────────
      if (millis() - lastWatchdog > 5000) {
        lastWatchdog = millis();
        Wire.beginTransmission(PCA_ADDR);
        uint8_t i2cErr = Wire.endTransmission();
        
        if (i2cErr == 0) {
          if (!pcaFound) {
            DEBUG_PRINTLN(F("LumiGrid PCA9685: chip found (recovered)"));
            pcaFound = true;
            pwm.begin();
            pwm.setPWMFreq(pwmFrequency);
          }
        } else {
          if (pcaFound) {
            DEBUG_PRINTLN(F("LumiGrid PCA9685: I2C error — lost chip"));
            pcaFound = false;
          }
        }
      }

      // ── Temperature reading every 2 s ────────────────────────────────────
      if (tempSensorFound && millis() - lastTempUpdate > 2000) {
        lastTempUpdate = millis();
        sensors->requestTemperatures();
        currentTemp = sensors->getTempCByIndex(0);
      }

      // ── Emergency button ─────────────────────────────────────────────────
      if (emergencyBtnPin >= 0) {
        bool reading = digitalRead(emergencyBtnPin);
        if (reading == LOW && lastBtnState == HIGH) {
          btnPressTime = millis();
        } else if (reading == HIGH && lastBtnState == LOW) {
          unsigned long dur = millis() - btnPressTime;
          if (dur > 1500) {
            // Long press → toggle emergency-off
            emergencyOff = !emergencyOff;
            if (emergencyOff) {
              // Drive all channels to 0 immediately
              for (uint8_t i = 0; i < NUM_CHANNELS; i++) applyPWM(i, 0);
            }
            DEBUG_PRINTF_P(PSTR("LumiGrid: emergencyOff=%d\n"), (int)emergencyOff);
          } else if (dur > 50) {
            // Short press → WLED power toggle
            // toggleOnOff(); // Commented out to prevent double-toggle (WLED core handles BTNPIN)
          }
        }
        lastBtnState = reading;
      }

      // ── Rate-limit PWM writes to ~66 Hz ─────────────────────────────────
      if (millis() - lastUpdate < 15) return;
      lastUpdate = millis();

      // ── Map WLED pixel colors → PCA9685 channels ─────────────────────────
      // Each multi-channel type (CCT/RGB/RGBW) consumes one virtual pixel and
      // multiple consecutive PCA channels.
      uint16_t pixelIdx = 0;
      uint8_t  ch       = 0;

      while (ch < NUM_CHANNELS) {
        uint32_t color = strip.getPixelColor(pixelIdx);
        uint8_t  type  = channelType[ch];

        switch (type) {
          case 0: // Off — force channel dark
            applyPWM(ch, 0);
            ch++;
            break;

          case 1: { // Dumb PWM — brightness = max component of RGBW pixel
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >>  8) & 0xFF;
            uint8_t b = (color      ) & 0xFF;
            uint8_t w = (color >> 24) & 0xFF;
            uint8_t v = max(max(r, g), max(b, w));
            applyPWM(ch, mapTo12Bit(v, ch));
            ch++;
            pixelIdx++;
            break;
          }

          case 2: { // CCT — WW on ch, CW on ch+1
            if (ch + 1 >= NUM_CHANNELS) { applyPWM(ch, 0); ch++; break; }
            applyPWM(ch,     mapTo12Bit((color >> 16) & 0xFF, ch));
            applyPWM(ch + 1, mapTo12Bit((color >>  8) & 0xFF, ch + 1));
            ch += 2;
            pixelIdx++;
            break;
          }

          case 3: { // RGB
            if (ch + 2 >= NUM_CHANNELS) { applyPWM(ch, 0); ch++; break; }
            applyPWM(ch,     mapTo12Bit((color >> 16) & 0xFF, ch));
            applyPWM(ch + 1, mapTo12Bit((color >>  8) & 0xFF, ch + 1));
            applyPWM(ch + 2, mapTo12Bit((color      ) & 0xFF, ch + 2));
            ch += 3;
            pixelIdx++;
            break;
          }

          case 4: { // RGBW
            if (ch + 3 >= NUM_CHANNELS) { applyPWM(ch, 0); ch++; break; }
            applyPWM(ch,     mapTo12Bit((color >> 16) & 0xFF, ch));
            applyPWM(ch + 1, mapTo12Bit((color >>  8) & 0xFF, ch + 1));
            applyPWM(ch + 2, mapTo12Bit((color      ) & 0xFF, ch + 2));
            applyPWM(ch + 3, mapTo12Bit((color >> 24) & 0xFF, ch + 3));
            ch += 4;
            pixelIdx++;
            break;
          }

          default:
            applyPWM(ch, 0);
            ch++;
            break;
        }
      }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // JSON info (shown on the WLED Info page and /json/info)
    // ─────────────────────────────────────────────────────────────────────────

    void addToJsonInfo(JsonObject& root) override {
      if (!enabled) return;
      JsonObject user = root.containsKey("u") ? root["u"].as<JsonObject>() : root.createNestedObject("u");
      JsonObject pca  = user.createNestedObject(FPSTR(_name));

      JsonArray tempArr = pca.createNestedArray("temp");
      if (!tempSensorFound) {
        tempArr.add(0);
        tempArr.add(F(" °C (no sensor — set tempPin in config)"));
      } else {
        tempArr.add(currentTemp);
        tempArr.add(F(" °C"));
      }
      pca["chip"]         = pcaFound;
      pca["addr"]         = F("0x40");
      pca["channels"]     = NUM_CHANNELS;
      pca["pwmFreq"]      = pwmFrequency;
      pca["emergencyOff"] = emergencyOff;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Persistent configuration (written to / read from cfg.json)
    // ─────────────────────────────────────────────────────────────────────────

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top["enabled"] = enabled;
      top["tempPin"] = tempPin;
      top["btnPin"]  = emergencyBtnPin;
      top["pwmFreq"] = pwmFrequency;
      for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        top["t" + String(i)] = channelType[i];
        top["l" + String(i)] = channelLabel[i];
        top["c" + String(i)] = channelCalibration[i];
      }
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) return false;

      bool ok = true;
      ok &= getJsonValue(top["enabled"], enabled);

      int8_t oldTempPin = tempPin;
      getJsonValue(top["tempPin"], tempPin);
      getJsonValue(top["btnPin"],  emergencyBtnPin);

      // PWM frequency — clamped to PCA9685 hardware limits [24, 1526] Hz
      uint16_t newFreq = pwmFrequency;
      if (getJsonValue(top["pwmFreq"], newFreq)) {
        newFreq = constrain(newFreq, (uint16_t)24u, (uint16_t)1526u);
        if (newFreq != pwmFrequency) {
          pwmFrequency = newFreq;
          if (initDone && pcaFound) pwm.setPWMFreq(pwmFrequency);
        }
      }

      // Per-channel settings
      for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
        getJsonValue(top["t" + String(i)], channelType[i]);
        String lbl;
        if (getJsonValue(top["l" + String(i)], lbl)) {
          strlcpy(channelLabel[i], lbl.c_str(), sizeof(channelLabel[i]));
        }
        uint8_t cal = 100;
        if (getJsonValue(top["c" + String(i)], cal)) {
          channelCalibration[i] = constrain(cal, (uint8_t)0u, (uint8_t)100u);
        }
      }

      // Re-init temp sensor only if the pin actually changed
      if (tempPin != oldTempPin && initDone) setupTemperature();

      return ok;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Settings UI — injected JavaScript rendered by the WLED settings page.
    // Buffer limit: ~3840 bytes on ESP32.  8 channels × 5 options = manageable.
    // ─────────────────────────────────────────────────────────────────────────

    void appendConfigData() override {
      // ── Section header hint ──────────────────────────────────────────────
      oappend(F("addInfo('"));
      oappend(String(FPSTR(_name)).c_str());
      oappend(F(":enabled',1,'<b>LumiGrid PCA9685 — 8-ch PWM controller (LED0–LED7)</b>');"));

      // ── Per-channel type dropdowns (LED0–LED7) ────────────────────────────
      oappend(F("for(var i=0;i<8;i++){"));
      oappend(F("var d=addDropdown('"));
      oappend(String(FPSTR(_name)).c_str());
      oappend(F("','t'+i);"));
      oappend(F("addOption(d,'Off',0);"));
      oappend(F("addOption(d,'Dumb PWM',1);"));
      oappend(F("addOption(d,'CCT (2ch)',2);"));
      oappend(F("addOption(d,'RGB (3ch)',3);"));
      oappend(F("addOption(d,'RGBW (4ch)',4);"));
      oappend(F("}"));

      // ── Hint: calibration fields ──────────────────────────────────────────
      oappend(F("addInfo('"));
      oappend(String(FPSTR(_name)).c_str());
      oappend(F(":c0',1,'<i>0–100: per-channel brightness cap (%)</i>');"));

      // ── Hint: pwmFreq field ───────────────────────────────────────────────
      oappend(F("addInfo('"));
      oappend(String(FPSTR(_name)).c_str());
      oappend(F(":pwmFreq',1,'<i>PCA9685 PWM frequency in Hz (24–1526). Default: 1000</i>');"));

      // ── Hint: tempPin field ───────────────────────────────────────────────
      oappend(F("addInfo('"));
      oappend(String(FPSTR(_name)).c_str());
      oappend(F(":tempPin',1,'<i>DS18B20 GPIO (-1 = disabled). Must NOT be GPIO 4/17/18/19/23/26/27</i>');"));

      // ── Hint: btnPin field ────────────────────────────────────────────────
      oappend(F("addInfo('"));
      oappend(String(FPSTR(_name)).c_str());
      oappend(F(":btnPin',1,'<i>Emergency-off button GPIO (active-low, internal pull-up). Default: 32</i>');"));
    }

    uint16_t getId() override { return USERMOD_ID_LUMIGRID_PCA9685; }
};

const char UsermodLumigridPCA9685::_name[] PROGMEM = "Lumigrid_PCA9685";
static UsermodLumigridPCA9685 lumigridPCA9685;
REGISTER_USERMOD(lumigridPCA9685);
