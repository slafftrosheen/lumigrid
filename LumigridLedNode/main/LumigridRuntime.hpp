#pragma once

#include "OutputController.hpp"
#include "ControlSourceManager.hpp"
#include "ConfigStore.hpp"
#include "LightingRuntime.hpp"
#include "Scheduler.hpp"
#include "NodeSync.hpp"
#include "WifiService.hpp"
#include "MdnsService.hpp"
#include "ArtnetDiscovery.hpp"
#include "SacnReceiver.hpp"
#include "DmxRouter.hpp"
#include "MqttService.hpp"
#include "FactoryRunner.hpp"
#include "FactoryApi.hpp"
#include "esp_err.h"

namespace lg {

class LumigridRuntime {
public:
    LumigridRuntime();
    ~LumigridRuntime() = default;

    esp_err_t init();
    esp_err_t start();

private:
    static void recovery_task(void* arg);

    OutputController output_;
    ControlSourceManager source_manager_;
    ConfigStore config_;
    FactoryRunner factory_runner_;
    FactoryApi api_;
    WifiService wifi_;
    MdnsService mdns_;
    ArtnetDiscovery artnet_;
    SacnReceiver sacn_;
    LightingRuntime lighting_;
    DmxRouter dmx_router_;
    ClockDiscipline clock_;
    Scheduler scheduler_;
    NodeSync node_sync_;
    MqttService mqtt_;

    bool safe_mode_ = false;
};

} // namespace lg
