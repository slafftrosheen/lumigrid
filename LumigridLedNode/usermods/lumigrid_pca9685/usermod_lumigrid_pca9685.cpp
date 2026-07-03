#pragma once

#include "wled.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class UsermodLumigridPCA9685 : public Usermod {
  private:
    bool enabled = true;
    bool initDone = false;
    
    // 0=Off, 1=Dumb PWM, 2=CCT, 3=RGB, 4=RGBW
    uint8_t pcaType[32]; 
    String channelLabels[32];
    
    int8_t emergencyBtnPin = 32; // Default to GPIO 32
    bool lastBtnState = HIGH;
    unsigned long btnPressTime = 0;
    bool emergencyOff = false;

    int8_t tempPin = 4; // Default to GPIO 4
    float currentTemp = 0.0;
    unsigned long lastTempUpdate = 0;
    bool tempSensorFound = false;

    OneWire* oneWire = nullptr;
    DallasTemperature* sensors = nullptr;

    uint8_t  layoutChannelCount = 0;
    uint8_t  chipChannelOffset[2] = {0, 0};
    uint8_t  channelCalibration[32];
    uint8_t  chipAddr[2] = {0x40, 0x41};

    // Macro preset support
    static constexpr uint8_t MACRO_COUNT = 4;
    String macroNames[MACRO_COUNT];
    uint8_t macroTypes[MACRO_COUNT][32];
    bool macroConfigured = false;

    uint32_t lastUpdate = 0;
    Adafruit_PWMServoDriver pwm1 = Adafruit_PWMServoDriver(0x40);
    Adafruit_PWMServoDriver pwm2 = Adafruit_PWMServoDriver(0x41);
    bool pca1Found = false;
    bool pca2Found = false;

    // Watchdog
    unsigned long lastWatchdogCheck = 0;

    static const char _name[];

    void setupTemperature() {
      if (tempPin >= 0) {
        if (oneWire) delete oneWire;
        if (sensors) delete sensors;
        oneWire = new OneWire(tempPin);
        sensors = new DallasTemperature(oneWire);
        sensors->begin();
        tempSensorFound = (sensors->getDeviceCount() > 0);
      }
    }

  public:
    UsermodLumigridPCA9685() {
      for(int i=0; i<32; i++) {
        pcaType[i] = 1;
        channelLabels[i] = "Channel " + String(i);
        channelCalibration[i] = 100;
      }
      chipChannelOffset[0] = 0;
      chipChannelOffset[1] = 16;
      macroConfigured = false;
      for (int m=0; m<MACRO_COUNT; m++) {
        macroNames[m] = "Preset " + String(m+1);
        for (int i=0; i<32; i++) macroTypes[m][i] = pcaType[i];
      }
    }

    void rebuildLayout() {
      uint8_t pca_ch = 0;
      chipChannelOffset[0] = 0;
      for (int i=0; i<32; i++) {
        uint8_t t = pcaType[i];
        if (t == 0) {
          // Off
        } else if (t == 1) {
          pca_ch += 1;
        } else if (t == 2) {
          pca_ch += 2;
        } else if (t == 3) {
          pca_ch += 3;
        } else if (t == 4) {
          pca_ch += 4;
        } else {
          pca_ch += 1;
        }
      }
      chipChannelOffset[1] = pca_ch;
      layoutChannelCount = min<uint8_t>(pca_ch, 32);
    }
    void setup() override {

      #ifdef PCA9685_OE
      pinMode(PCA9685_OE, OUTPUT);
      digitalWrite(PCA9685_OE, LOW);
      #else
      pinMode(25, OUTPUT);
      digitalWrite(25, LOW);
      #endif
      
      Wire.begin();
      
      // I2C Scanner
      Wire.beginTransmission(0x40);
      if (Wire.endTransmission() == 0) {
        pca1Found = true;
        pwm1.begin();
        pwm1.setPWMFreq(1500);
      }
      
      Wire.beginTransmission(0x41);
      if (Wire.endTransmission() == 0) {
        pca2Found = true;
        pwm2.begin();
        pwm2.setPWMFreq(1500);
      }

      if (emergencyBtnPin >= 0) {
        pinMode(emergencyBtnPin, INPUT_PULLUP);
      }
      
      setupTemperature();
      
      rebuildLayout();

      initDone = true;
      initDone = true;
    }

    uint16_t mapTo12Bit(uint8_t val) {
      if (emergencyOff) return 0;
      
      // Thermal throttling
      float multiplier = 1.0;
      if (currentTemp > 60.0) multiplier = 0.5;
      if (currentTemp > 75.0) multiplier = 0.1;
      if (currentTemp > 85.0) multiplier = 0.0; // Shut down completely

      val = (uint16_t(val) * uint16_t(channelCalibration[0])) / 100;

      uint32_t val32 = val;
      uint32_t bri32 = bri * multiplier;
      uint16_t pwmVal = (val32 * bri32 * 4095) / 65025;
      return (uint32_t(pwmVal) * uint32_t(pwmVal)) / 4095;
    }

    void applyPWM(uint8_t ch, uint16_t val) {
      if (ch < 16 && pca1Found) {
        pwm1.setPWM(ch, 0, val);
      } else if (ch >= 16 && pca2Found) {
        pwm2.setPWM(ch - 16, 0, val);
      }
    }

    void loop() override {
      if (!enabled || !initDone || strip.isUpdating()) return;

      // Watchdog
      if (millis() - lastWatchdogCheck > 5000) {
        lastWatchdogCheck = millis();
        if (pca1Found) {
           Wire.beginTransmission(0x40);
           if (Wire.endTransmission() != 0) {
             DEBUG_PRINTLN(F("PCA1 I2C ERROR"));
             pwm1.begin(); // Retry
           }
        }
        if (pca2Found) {
           Wire.beginTransmission(0x41);
           if (Wire.endTransmission() != 0) {
             DEBUG_PRINTLN(F("PCA2 I2C ERROR"));
             pwm2.begin(); // Retry
           }
        }
      }

      // Temp sensor reading
      if (tempSensorFound && millis() - lastTempUpdate > 2000) {
        lastTempUpdate = millis();
        sensors->requestTemperatures();
        currentTemp = sensors->getTempCByIndex(0);
      }

      // Button handler
      if (emergencyBtnPin >= 0) {
        bool reading = digitalRead(emergencyBtnPin);
        if (reading == LOW && lastBtnState == HIGH) {
          btnPressTime = millis();
        } else if (reading == HIGH && lastBtnState == LOW) {
          unsigned long duration = millis() - btnPressTime;
          if (duration > 1500) {
             // Long press -> Emergency Off
             emergencyOff = !emergencyOff;
          } else if (duration > 50) {
             // Short press -> toggle power
             toggleOnOff();
          }
        }
        lastBtnState = reading;
      }

      if (millis() - lastUpdate < 15) return;
      lastUpdate = millis();

      uint8_t pca_ch = 0;
      uint16_t virtual_pixel_idx = 0;
      uint8_t total_channels = pca2Found ? 32 : 16;

      while (pca_ch < total_channels) {
        uint8_t type = pcaType[pca_ch];
        uint32_t color = strip.getPixelColor(virtual_pixel_idx);
        
        if (type == 0) {
          applyPWM(pca_ch, 0);
          pca_ch += 1;
        } 
        else if (type == 1) {
          uint8_t w = max(max((color >> 16) & 0xFF, (color >> 8) & 0xFF), max((color >> 0) & 0xFF, (color >> 24) & 0xFF));
          applyPWM(pca_ch, mapTo12Bit(w));
          pca_ch += 1;
          virtual_pixel_idx += 1;
        }
        else if (type == 2) {
          if (pca_ch + 1 >= total_channels) { applyPWM(pca_ch, 0); break; }
          uint8_t ww = (color >> 16) & 0xFF;
          uint8_t cw = (color >> 8) & 0xFF;
          applyPWM(pca_ch, mapTo12Bit(ww));
          applyPWM(pca_ch + 1, mapTo12Bit(cw));
          pca_ch += 2;
          virtual_pixel_idx += 1;
        }
        else if (type == 3) {
          if (pca_ch + 2 >= total_channels) { applyPWM(pca_ch, 0); break; }
          applyPWM(pca_ch, mapTo12Bit((color >> 16) & 0xFF));
          applyPWM(pca_ch + 1, mapTo12Bit((color >> 8) & 0xFF));
          applyPWM(pca_ch + 2, mapTo12Bit((color >> 0) & 0xFF));
          pca_ch += 3;
          virtual_pixel_idx += 1;
        }
        else if (type == 4) {
          if (pca_ch + 3 >= total_channels) { applyPWM(pca_ch, 0); break; }
          applyPWM(pca_ch, mapTo12Bit((color >> 16) & 0xFF));
          applyPWM(pca_ch + 1, mapTo12Bit((color >> 8) & 0xFF));
          applyPWM(pca_ch + 2, mapTo12Bit((color >> 0) & 0xFF));
          applyPWM(pca_ch + 3, mapTo12Bit((color >> 24) & 0xFF));
          pca_ch += 4;
          virtual_pixel_idx += 1;
        } else {
          applyPWM(pca_ch, 0);
          pca_ch += 1;
        }
      }
    }

    void addToJsonInfo(JsonObject& root) override {
      if (!enabled) return;
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");
      
      JsonObject pca = user.createNestedObject(FPSTR(_name));
      {
        JsonArray tempArr = pca.createNestedArray("temp");
        if (!tempSensorFound) {
          tempArr.add(0);
          tempArr.add(F(" Sensor Not Found"));
        } else {
          tempArr.add(currentTemp);
          tempArr.add(F(" °C"));
        }
      }
      pca["emergencyOff"]    = emergencyOff;
      pca["layoutChannels"]  = layoutChannelCount;
      pca["chipA"]           = pca1Found;
      pca["chipB"]           = pca2Found;
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top["enabled"] = enabled;
      top["tempPin"] = tempPin;
      top["emergencyBtnPin"] = emergencyBtnPin;
      
      for (int i=0; i<32; i++) {
        top["type_" + String(i)] = pcaType[i];
        top["label_" + String(i)] = channelLabels[i];
        top["cal_" + String(i)] = channelCalibration[i];
      }
      top["groupPins"] = pca2Found;
      top["chipAaddr"] = chipAddr[0];
      top["chipBaddr"] = chipAddr[1];
      
      for (int m=0; m<MACRO_COUNT; m++) {
        top[("mname_"+String(m))] = macroNames[m];
        for (int i=0; i<32; i++) {
          top[("mtype_"+String(m)+"_"+String(i))] = macroTypes[m][i];
        }
      }
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) return false;
      bool configComplete = !top.isNull();
      configComplete &= getJsonValue(top["enabled"], enabled);
      
      int8_t oldTemp = tempPin;
      getJsonValue(top["tempPin"], tempPin);
      getJsonValue(top["emergencyBtnPin"], emergencyBtnPin);
      
      if (tempPin != oldTemp && initDone) {
         setupTemperature();
      }

      for (int i=0; i<32; i++) {
        getJsonValue(top["type_" + String(i)], pcaType[i]);
        String lbl;
        if (getJsonValue(top["label_" + String(i)], lbl)) {
           channelLabels[i] = lbl;
        }
        uint8_t cal = 100;
        getJsonValue(top["cal_" + String(i)], cal);
        channelCalibration[i] = constrain(cal, 0, 255);
      }
      uint8_t g = 1;
      getJsonValue(top["groupPins"], g);
      pca2Found = g;
      uint8_t addrA = chipAddr[0], addrB = chipAddr[1];
      getJsonValue(top["chipAaddr"], addrA);
      getJsonValue(top["chipBaddr"], addrB);
      chipAddr[0] = addrA; chipAddr[1] = addrB;
      rebuildLayout();

      for (int m=0; m<MACRO_COUNT; m++) {
        String nm;
        getJsonValue(top[("mname_"+String(m))], nm);
        if (nm.length()) macroNames[m] = nm;
      }
      macroConfigured = false;
      for (int m=0; m<MACRO_COUNT; m++) {
        for (int i=0; i<32; i++) {
          uint8_t t = pcaType[i];
          getJsonValue(top[("mtype_"+String(m)+"_"+String(i))], t);
          if (macroConfigured || t != pcaType[i]) macroConfigured = true;
          macroTypes[m][i] = t;
        }
      }
      return configComplete;
    }



    uint16_t getId() override {
      return USERMOD_ID_LUMIGRID_PCA9685;
    }

    void appendConfigData() override {
      oappend(F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(F(":groupPins")); oappend(F("',1,'<i>(Enable if using 2nd PCA9685)</i>');"));
      
      // Inject Javascript to dynamically create the 32 channel type dropdowns
      // This saves flash and prevents oappend 3KB buffer overflow!
      oappend(F("for(let i=0;i<32;i++){"));
      oappend(F("let dd=addDropdown('")); oappend(String(FPSTR(_name)).c_str()); oappend(F("','type_'+i);"));
      oappend(F("addOption(dd,'Off',0);"));
      oappend(F("addOption(dd,'Dumb PWM',1);"));
      oappend(F("addOption(dd,'CCT',2);"));
      oappend(F("addOption(dd,'RGB',3);"));
      oappend(F("addOption(dd,'RGBW',4);"));
      oappend(F("}"));

      // Dropdowns for the 4 macro presets
      oappend(F("for(let m=0;m<4;m++){"));
      oappend(F("for(let i=0;i<32;i++){"));
      oappend(F("let dd=addDropdown('")); oappend(String(FPSTR(_name)).c_str()); oappend(F("','mtype_'+m+'_'+i);"));
      oappend(F("addOption(dd,'Off',0);"));
      oappend(F("addOption(dd,'Dumb PWM',1);"));
      oappend(F("addOption(dd,'CCT',2);"));
      oappend(F("addOption(dd,'RGB',3);"));
      oappend(F("addOption(dd,'RGBW',4);"));
      oappend(F("}}"));
    }

    // Apply a stored preset by index (0..MACRO_COUNT-1)
    void applyPreset(uint8_t presetIndex) {
      if (presetIndex >= MACRO_COUNT) return;
      memcpy(pcaType, macroTypes[presetIndex], sizeof(pcaType));
      rebuildLayout();
    }
  };

const char UsermodLumigridPCA9685::_name[] PROGMEM = "Lumigrid_PCA9685";
static UsermodLumigridPCA9685 lumigridPCA9685;
REGISTER_USERMOD(lumigridPCA9685);
