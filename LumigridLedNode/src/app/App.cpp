#include "app/App.h"
#include "app/TaskSupervisor.h"
#include "board/BoardSafety.h"
#include "config/lg_config_store.hpp"
#include "diagnostics/Metrics.h"
#include "network/WifiManager.h"
#include "network/WsHub.h"
#include "network/ApiServer.h"
#include "output/PixelBus.h"
#include "output/PwmBus.h"
#include "protocols/protocol_router.hpp"
#include "render/Renderer.h"

namespace app {
static TaskSupervisor tasks;
static lg::ProtocolRouter router;
static render::Renderer renderer;
static output::PixelBus pixels;
static output::PwmBus pwm;
static lg::ConfigStore cfgStore;

void setup() {
  Serial.begin(115200);
  delay(50);

  Metrics::init();
  board::safetyInit();
  board::disableOutputs();

  if (!pwm.probe()) {
    Metrics::recordPcaFault();
    board::safeOff();
    Serial.println("PCA9685 missing; safe-off");
    for (;;) delay(1000);
  }

  if (!pixels.probe()) {
    Metrics::recordPixelFault();
    board::safeOff();
    Serial.println("Pixel bus fault; safe-off");
    for (;;) delay(1000);
  }

  lg::NodeConfig cfg;
  cfgStore.load(cfg);

  renderer.init(cfg);
  pixels.init(cfg);
  pwm.init(cfg);

  wifi.begin(cfg.node_id.c_str());
  wsHub.begin();
  apiServer.begin(nullptr, nullptr, nullptr, nullptr, nullptr);
  router.begin(cfg);

  tasks.start(router);
  tasks.loop();

  board::enableOutputs();
  Serial.println("Lumigrid node up");
}

void loop() {
  wifi.loop();
  apiServer.loop();
  wsHub.loop();
  router.loop();
  Metrics::tick();
  tasks.loop();
  delay(2);
}
}
