#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <Arduino_JSON.h>

// Required for disabling the brownout detector (ESP32)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// =================================================================
// --- APPLICATION CONFIGURATION ---
// =================================================================

#define APP_DEBUG 1 // 1=enable Serial debug, 0=production

#if APP_DEBUG
  #define DBG(...) do { Serial.printf("[%-10lu] ", millis()); Serial.printf(__VA_ARGS__); } while(0)
#else
  #define DBG(...) do {} while(0)
#endif

const char* ap_ssid     = "CubeLedRF";
const char* ap_password = "cube1234";

// --- Pin Definitions ---
#define LED_BUILTIN  2        // On-board LED (GPIO2)
#define SPI_SCK      18
#define SPI_MISO     19
#define SPI_MOSI     23
#define CC1101_CSN   5
#define CC1101_GDO0  15       // Moved off GPIO2 to avoid conflict
#define CC1101_GDO2  4
#define RED_PIN      25
#define GREEN_PIN    26
#define BLUE_PIN     27
#define WHITE_PIN    32

// --- Remote Button Codes ---
#define BUTTON1_CODE        22369749UL
#define BUTTON2_CODE        22369653UL
#define RF_BITLENGTH        25
#define RF_PROTOCOL         5

// =================================================================
// --- GLOBALS ---
// =================================================================

struct RgbwColor { uint8_t r, g, b, w; };

enum UpdateType {
  PRESS_BUTTON_1,
  PRESS_BUTTON_2,
  SAVE_BUTTON_1,
  SAVE_BUTTON_2
};

struct UpdateRequest {
  UpdateType type;
  RgbwColor color; // used for SAVE_*
};

volatile RgbwColor button1_color = {28, 78, 255, 0};
volatile RgbwColor button2_color = {0, 0, 0, 255};

QueueHandle_t updateQueue;
RCSwitch mySwitch = RCSwitch();
AsyncWebServer server(80);
Preferences preferences;

// cross-core guard for reading/writing volatile structs
portMUX_TYPE colorMux = portMUX_INITIALIZER_UNLOCKED;

// LEDC PWM setup
const int LEDC_FREQ = 5000; // 5 kHz
const int LEDC_RES  = 8;    // 8-bit resolution
const int CH_R = 0, CH_G = 1, CH_B = 2, CH_W = 3;

// If your LED hardware is common-anode, set true to invert PWM
const bool COMMON_ANODE = false;

// =================================================================
// --- DEBUG HELPERS ---
// =================================================================

static inline uint8_t u8clamp(int v){
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

void dumpColor(const char* tag, const RgbwColor& c) {
  DBG("%s => R:%3u G:%3u B:%3u W:%3u\n", tag, c.r, c.g, c.b, c.w);
}

void dumpVolatile(const char* tag, volatile RgbwColor& c) {
  DBG("%s => R:%3u G:%3u B:%3u W:%3u (volatile)\n", tag, c.r, c.g, c.b, c.w);
}

const char* typeName(UpdateType t){
  switch(t){
    case PRESS_BUTTON_1: return "PRESS_BUTTON_1";
    case PRESS_BUTTON_2: return "PRESS_BUTTON_2";
    case SAVE_BUTTON_1:  return "SAVE_BUTTON_1";
    case SAVE_BUTTON_2:  return "SAVE_BUTTON_2";
  }
  return "UNKNOWN";
}

// =================================================================
// --- WEB INTERFACE (HTML) ---
// =================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>CubeLedRF Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <style>
    :root { --clr-bg:#282c34; --clr-fg:#fff; --clr-primary:#61dafb; --clr-container:#20232a; }
    body { font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; background:var(--clr-bg); color:var(--clr-fg); margin:0; padding:15px; text-align:center; }
    h1 { color:var(--clr-primary); font-weight:300; }
    .container { max-width:420px; margin:auto; background:var(--clr-container); padding:20px; border-radius:10px; box-shadow:0 4px 8px rgba(0,0,0,0.3); margin-bottom:20px; }
    .slider-group { display:flex; align-items:center; gap:10px; margin-bottom:10px; }
    .slider-group label { width:28px; text-align:left; opacity:.9; }
    input[type=range]{ -webkit-appearance:none; appearance:none; flex:1; height:10px; border-radius:5px; outline:none; }
    input[type=range]::-webkit-slider-thumb{ -webkit-appearance:none; appearance:none; width:20px; height:20px; background:var(--clr-primary); border-radius:50%; cursor:pointer; }
    input[type=range].red-slider{ background:linear-gradient(to right,#444,#f00); }
    input[type=range].green-slider{ background:linear-gradient(to right,#444,#0f0); }
    input[type=range].blue-slider{ background:linear-gradient(to right,#444,#00f); }
    input[type=range].white-slider{ background:linear-gradient(to right,#444,#fff); }
    .slider-value{ width:35px; text-align:right; font-variant-numeric:tabular-nums; }
    .color-swatch{ width:60px; height:60px; border-radius:50%; margin:15px auto; border:2px solid rgba(255,255,255,.1); }
    button{ background:var(--clr-primary); color:var(--clr-container); border:none; padding:12px 24px; font-size:16px; border-radius:8px; cursor:pointer; width:100%; }
    .status{ margin-top:12px; font-style:italic; color:#9aa; min-height:20px; }
  </style>
</head>
<body>
  <h1>RGBW Web Controller</h1>

  <div class="container">
    <h2>Button 1</h2>
    <div class="slider-group"><label>R</label><input type="range" class="red-slider"   id="r1" min="0" max="255"><span class="slider-value" id="r1-val">0</span></div>
    <div class="slider-group"><label>G</label><input type="range" class="green-slider" id="g1" min="0" max="255"><span class="slider-value" id="g1-val">0</span></div>
    <div class="slider-group"><label>B</label><input type="range" class="blue-slider"  id="b1" min="0" max="255"><span class="slider-value" id="b1-val">0</span></div>
    <div class="slider-group"><label>W</label><input type="range" class="white-slider" id="w1" min="0" max="255"><span class="slider-value" id="w1-val">0</span></div>
    <div class="color-swatch" id="swatch1"></div>
    <button onclick="saveColor(1)">Save Color 1</button>
    <div id="status1" class="status"></div>
  </div>

  <div class="container">
    <h2>Button 2</h2>
    <div class="slider-group"><label>R</label><input type="range" class="red-slider"   id="r2" min="0" max="255"><span class="slider-value" id="r2-val">0</span></div>
    <div class="slider-group"><label>G</label><input type="range" class="green-slider" id="g2" min="0" max="255"><span class="slider-value" id="g2-val">0</span></div>
    <div class="slider-group"><label>B</label><input type="range" class="blue-slider"  id="b2" min="0" max="255"><span class="slider-value" id="b2-val">0</span></div>
    <div class="slider-group"><label>W</label><input type="range" class="white-slider" id="w2" min="0" max="255"><span class="slider-value" id="w2-val">0</span></div>
    <div class="color-swatch" id="swatch2"></div>
    <button onclick="saveColor(2)">Save Color 2</button>
    <div id="status2" class="status"></div>
  </div>

  <script>
    function clamp255(v){ v = parseInt(v||0,10); if(isNaN(v)) v=0; return Math.max(0, Math.min(255, v)); }

    function updateSwatch(btnNum){
      const r = clamp255(document.getElementById(`r${btnNum}`).value);
      const g = clamp255(document.getElementById(`g${btnNum}`).value);
      const b = clamp255(document.getElementById(`b${btnNum}`).value);
      const w = clamp255(document.getElementById(`w${btnNum}`).value);
      const rd = Math.min(255, r + w);
      const gd = Math.min(255, g + w);
      const bd = Math.min(255, b + w);
      const swatch = document.getElementById(`swatch${btnNum}`);
      swatch.style.backgroundColor = `rgb(${rd}, ${gd}, ${bd})`;
    }

    function attachSlider(id, btnNum){
      const slider = document.getElementById(id);
      const val    = document.getElementById(id+'-val');
      slider.addEventListener('input', () => { val.textContent = slider.value; updateSwatch(btnNum); });
    }

    function initSliders(btnNum, colors){
      ['r','g','b','w'].forEach(ch => {
        const slider = document.getElementById(`${ch}${btnNum}`);
        const valueSpan = document.getElementById(`${ch}${btnNum}-val`);
        const key = `${ch}${btnNum}`;
        slider.value = clamp255(colors[key] ?? 0);
        valueSpan.textContent = slider.value;
        attachSlider(`${ch}${btnNum}`, btnNum);
      });
      updateSwatch(btnNum);
    }

    async function saveColor(btnNum){
      const r = clamp255(document.getElementById(`r${btnNum}`).value);
      const g = clamp255(document.getElementById(`g${btnNum}`).value);
      const b = clamp255(document.getElementById(`b${btnNum}`).value);
      const w = clamp255(document.getElementById(`w${btnNum}`).value);
      const statusDiv = document.getElementById(`status${btnNum}`);
      statusDiv.textContent = "Saving...";

      try{
        const res = await fetch(`/setcolor?button=${btnNum}&r=${r}&g=${g}&b=${b}&w=${w}`);
        const txt = await res.text();
        if(!res.ok){
          statusDiv.textContent = `Error ${res.status}: ${txt || 'Request failed'}`;
        }else{
          statusDiv.textContent = txt || "Saved.";
          setTimeout(()=> statusDiv.textContent = "", 2000);
        }
      }catch(e){
        console.error(e);
        statusDiv.textContent = "Error: Connection failed.";
      }
    }

    window.addEventListener('load', async () => {
      try{
        const res = await fetch('/getcolors');
        const colors = await res.json();
        initSliders(1, colors);
        initSliders(2, colors);
      }catch(e){
        console.error(e);
        alert('Failed to load initial colors.');
      }
    });
  </script>
</body>
</html>
)rawliteral";

// =================================================================
/*                    LEDC / COLORS / NVS HELPERS                  */
// =================================================================

void setRgbwColor(const RgbwColor& c) {
  auto toOut = [&](uint8_t v){ return COMMON_ANODE ? 255 - v : v; };
  ledcWrite(CH_R, toOut(c.r));
  ledcWrite(CH_G, toOut(c.g));
  ledcWrite(CH_B, toOut(c.b));
  ledcWrite(CH_W, toOut(c.w));
  dumpColor("LEDC write", c);
}

void saveLastActiveColor(const RgbwColor& color) {
  preferences.begin("state", false);
  preferences.putUChar("last_r", color.r);
  preferences.putUChar("last_g", color.g);
  preferences.putUChar("last_b", color.b);
  preferences.putUChar("last_w", color.w);
  preferences.putBool("state_saved", true);
  preferences.end();
  dumpColor("NVS saved last", color);
}

void saveButtonPreset(int buttonNum, const RgbwColor& color) {
  preferences.begin("colors", false);
  String prefix = "b" + String(buttonNum);
  preferences.putUChar((prefix + "r").c_str(), color.r);
  preferences.putUChar((prefix + "g").c_str(), color.g);
  preferences.putUChar((prefix + "b").c_str(), color.b);
  preferences.putUChar((prefix + "w").c_str(), color.w);
  preferences.end();
  DBG("NVS saved preset b%d\n", buttonNum);
  dumpColor("Preset saved", color);
}

void loadInitialState() {
  // Load button presets into volatile globals (member-by-member)
  preferences.begin("colors", true);
  portENTER_CRITICAL(&colorMux);
  ((volatile uint8_t&)button1_color.r) = preferences.getUChar("b1r", 28);
  ((volatile uint8_t&)button1_color.g) = preferences.getUChar("b1g", 78);
  ((volatile uint8_t&)button1_color.b) = preferences.getUChar("b1b", 255);
  ((volatile uint8_t&)button1_color.w) = preferences.getUChar("b1w", 0);
  ((volatile uint8_t&)button2_color.r) = preferences.getUChar("b2r", 0);
  ((volatile uint8_t&)button2_color.g) = preferences.getUChar("b2g", 0);
  ((volatile uint8_t&)button2_color.b) = preferences.getUChar("b2b", 0);
  ((volatile uint8_t&)button2_color.w) = preferences.getUChar("b2w", 255);
  portEXIT_CRITICAL(&colorMux);
  preferences.end();

  dumpVolatile("Button1 preset", button1_color);
  dumpVolatile("Button2 preset", button2_color);

  // Load last active color and set LEDs
  preferences.begin("state", true);
  RgbwColor lastColor;
  if (preferences.getBool("state_saved", false)) {
    lastColor.r = preferences.getUChar("last_r", 28);
    lastColor.g = preferences.getUChar("last_g", 78);
    lastColor.b = preferences.getUChar("last_b", 255);
    lastColor.w = preferences.getUChar("last_w", 0);
    DBG("NVS last state found.\n");
  } else {
    lastColor = {28, 78, 255, 0}; // default blue
    DBG("NVS last state not found; writing default.\n");
    preferences.end();
    saveLastActiveColor(lastColor); // write once
    preferences.begin("state", true);
  }
  preferences.end();
  dumpColor("Last active (load)", lastColor);
  setRgbwColor(lastColor);
}

// =================================================================
// --- RTOS TASKS ---
// =================================================================

void stateManagerTask(void* pvParameters) {
  DBG("StateManager started.\n");
  UpdateRequest request;
  for (;;) {
    if (xQueueReceive(updateQueue, &request, portMAX_DELAY) == pdPASS) {
      DBG("StateManager dequeued: %s (queue now %u)\n",
          typeName(request.type), (unsigned)uxQueueMessagesWaiting(updateQueue));
      RgbwColor colorToApply;

      switch (request.type) {
        case PRESS_BUTTON_1: {
          portENTER_CRITICAL(&colorMux);
          colorToApply.r = button1_color.r;
          colorToApply.g = button1_color.g;
          colorToApply.b = button1_color.b;
          colorToApply.w = button1_color.w;
          portEXIT_CRITICAL(&colorMux);
          dumpColor("Apply B1", colorToApply);
          setRgbwColor(colorToApply);
          saveLastActiveColor(colorToApply);
        } break;

        case PRESS_BUTTON_2: {
          portENTER_CRITICAL(&colorMux);
          colorToApply.r = button2_color.r;
          colorToApply.g = button2_color.g;
          colorToApply.b = button2_color.b;
          colorToApply.w = button2_color.w;
          portEXIT_CRITICAL(&colorMux);
          dumpColor("Apply B2", colorToApply);
          setRgbwColor(colorToApply);
          saveLastActiveColor(colorToApply);
        } break;

        case SAVE_BUTTON_1: {
          dumpColor("Save B1 req", request.color);
          portENTER_CRITICAL(&colorMux);
          ((volatile uint8_t&)button1_color.r) = request.color.r;
          ((volatile uint8_t&)button1_color.g) = request.color.g;
          ((volatile uint8_t&)button1_color.b) = request.color.b;
          ((volatile uint8_t&)button1_color.w) = request.color.w;
          portEXIT_CRITICAL(&colorMux);
          dumpVolatile("Button1 updated", button1_color);
          saveButtonPreset(1, request.color);
          setRgbwColor(request.color);
          saveLastActiveColor(request.color);
        } break;

        case SAVE_BUTTON_2: {
          dumpColor("Save B2 req", request.color);
          portENTER_CRITICAL(&colorMux);
          ((volatile uint8_t&)button2_color.r) = request.color.r;
          ((volatile uint8_t&)button2_color.g) = request.color.g;
          ((volatile uint8_t&)button2_color.b) = request.color.b;
          ((volatile uint8_t&)button2_color.w) = request.color.w;
          portEXIT_CRITICAL(&colorMux);
          dumpVolatile("Button2 updated", button2_color);
          saveButtonPreset(2, request.color);
          setRgbwColor(request.color);
          saveLastActiveColor(request.color);
        } break;
      }
    }
  }
}

void radioTask(void* pvParameters) {
  DBG("RadioTask started.\n");
  static unsigned long lastCode = 0;
  static uint32_t lastMs = 0;

  for (;;) {
    if (mySwitch.available()) {
      unsigned long value = mySwitch.getReceivedValue();
      uint8_t bitlen = mySwitch.getReceivedBitlength();
      uint8_t proto  = mySwitch.getReceivedProtocol();

      DBG("RF recv val:%lu (0x%lX) len:%u proto:%u\n", value, value, bitlen, proto);

      if (value != 0 && bitlen == RF_BITLENGTH && proto == RF_PROTOCOL) {
        uint32_t now = millis();
        if (value == lastCode && (now - lastMs) < 150) {
          DBG("RF filtered repeat within %u ms\n", (unsigned)(now - lastMs));
        } else {
          lastCode = value;
          lastMs = now;

          UpdateRequest request{};  // correct type
          bool matched = false;

          if (value == BUTTON1_CODE) {
            request.type = PRESS_BUTTON_1; matched = true;
          } else if (value == BUTTON2_CODE) {
            request.type = PRESS_BUTTON_2; matched = true;
          }

          if (matched) {
            DBG("RF matched => %s\n", typeName(request.type));
            digitalWrite(LED_BUILTIN, HIGH);
            if (xQueueSend(updateQueue, &request, pdMS_TO_TICKS(10)) != pdPASS) {
              DBG("Queue full; RF press dropped.\n");
            } else {
              DBG("Queued RF press (queue now %u)\n", (unsigned)uxQueueMessagesWaiting(updateQueue));
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            digitalWrite(LED_BUILTIN, LOW);
          } else {
            DBG("RF code not mapped to any action.\n");
          }
        }
      } else {
        DBG("RF invalid frame (len/proto mismatch or zero).\n");
      }

      mySwitch.resetAvailable();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =================================================================
// --- MAIN SETUP / LOOP ---
// =================================================================
void setup() {
#if APP_DEBUG
  Serial.begin(115200);
  while(!Serial) { delay(5); }
  DBG("Boot.\n");
#endif

  // Disable brownout detector (optional)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  DBG("Brownout detector disabled.\n");

  pinMode(LED_BUILTIN, OUTPUT);

  // LEDC setup for PWM
  ledcSetup(CH_R, LEDC_FREQ, LEDC_RES); ledcAttachPin(RED_PIN,   CH_R);
  ledcSetup(CH_G, LEDC_FREQ, LEDC_RES); ledcAttachPin(GREEN_PIN, CH_G);
  ledcSetup(CH_B, LEDC_FREQ, LEDC_RES); ledcAttachPin(BLUE_PIN,  CH_B);
  ledcSetup(CH_W, LEDC_FREQ, LEDC_RES); ledcAttachPin(WHITE_PIN, CH_W);
  DBG("LEDC configured: freq=%d Hz, res=%d-bit, pins R:%d G:%d B:%d W:%d\n",
      LEDC_FREQ, LEDC_RES, RED_PIN, GREEN_PIN, BLUE_PIN, WHITE_PIN);

  // Queue
  updateQueue = xQueueCreate(16, sizeof(UpdateRequest));
  DBG("Queue created (depth 16)\n");

  loadInitialState();

  // Wi-Fi AP
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress ip = WiFi.softAPIP();
  DBG("AP up SSID:\"%s\" pass:\"%s\" IP:%s\n", ap_ssid, ap_password, ip.toString().c_str());

  // --- Web Server ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    DBG("HTTP / index\n");
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    request->send(response);
  });

  server.on("/getcolors", HTTP_GET, [](AsyncWebServerRequest *request){
    RgbwColor b1, b2;
    portENTER_CRITICAL(&colorMux);
    b1.r = button1_color.r; b1.g = button1_color.g; b1.b = button1_color.b; b1.w = button1_color.w;
    b2.r = button2_color.r; b2.g = button2_color.g; b2.b = button2_color.b; b2.w = button2_color.w;
    portEXIT_CRITICAL(&colorMux);

    DBG("HTTP /getcolors\n");
    dumpColor(" -> get B1", b1);
    dumpColor(" -> get B2", b2);

    JSONVar colors;
    colors["r1"] = (int)b1.r; colors["g1"] = (int)b1.g; colors["b1"] = (int)b1.b; colors["w1"] = (int)b1.w;
    colors["r2"] = (int)b2.r; colors["g2"] = (int)b2.g; colors["b2"] = (int)b2.b; colors["w2"] = (int)b2.w;
    request->send(200, "application/json", JSON.stringify(colors));
  });

  server.on("/setcolor", HTTP_GET, [](AsyncWebServerRequest *request) {
    DBG("HTTP /setcolor\n");
    const char* keys[] = {"button","r","g","b","w"};
    for (auto k: keys) {
      if (!request->hasParam(k)) { DBG(" -> 400 Missing param: %s\n", k); request->send(400, "text/plain", "Missing param"); return; }
    }

    int btnNum = request->getParam("button")->value().toInt();
    if (btnNum != 1 && btnNum != 2) { DBG(" -> 400 Invalid button: %d\n", btnNum); request->send(400, "text/plain", "Invalid button"); return; }

    UpdateRequest updateReq;
    updateReq.type     = (btnNum == 1) ? SAVE_BUTTON_1 : SAVE_BUTTON_2;
    updateReq.color.r  = u8clamp(request->getParam("r")->value().toInt());
    updateReq.color.g  = u8clamp(request->getParam("g")->value().toInt());
    updateReq.color.b  = u8clamp(request->getParam("b")->value().toInt());
    updateReq.color.w  = u8clamp(request->getParam("w")->value().toInt());

    dumpColor(" -> req color", updateReq.color);
    DBG(" -> type %s, queue depth before %u\n", typeName(updateReq.type), (unsigned)uxQueueMessagesWaiting(updateQueue));

    if (xQueueSend(updateQueue, &updateReq, pdMS_TO_TICKS(50)) != pdPASS) {
      DBG(" -> 503 Busy (queue full)\n");
      request->send(503, "text/plain", "Busy. Try again.");
      return;
    }
    DBG(" -> 200 Saved (queue now %u)\n", (unsigned)uxQueueMessagesWaiting(updateQueue));
    request->send(200, "text/plain", "Saved.");
  });

  server.begin();
  DBG("Web server started.\n");

  // --- CC1101 Initialization ---
  DBG("CC1101 init...\n");
  ELECHOUSE_cc1101.setSpiPin(SPI_SCK, SPI_MISO, SPI_MOSI, CC1101_CSN);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  if (ELECHOUSE_cc1101.getCC1101()) {
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(433.92);
    ELECHOUSE_cc1101.SetRx();
    mySwitch.enableReceive(CC1101_GDO2);
    DBG("CC1101 OK @ 433.92 MHz. RX on GDO2 pin %d\n", CC1101_GDO2);
  } else {
    DBG("CC1101 NOT detected!\n");
  }

  // Tasks on Core 0
  xTaskCreatePinnedToCore(stateManagerTask, "StateManager", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(radioTask,       "RadioTask",    4096, NULL, 1, NULL, 0);
  DBG("Tasks created on Core0.\n");
}

void loop() {
  // All work is done in tasks; AsyncWebServer runs on the other core.
}
