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

enum class StartupState : uint8_t {
    Booting,
    Ready,
    SafeMode,
    Faulted
};

enum class StartupStage : uint8_t {
    None,
    OutputInitialized,
    SourceManagerInitialized,
    ConfigStoreInitialized,
    ConfigLoaded,
    ConfigValidated,
    OutputsConfigured,
    LightingStarted,
    FactoryStarted,
    NetworkConfigured,
    ProtocolsStarted,
    RuntimeStarted
};

inline const char* startup_state_to_name(StartupState st) {
    switch (st) {
        case StartupState::Booting:  return "Booting";
        case StartupState::Ready:    return "Ready";
        case StartupState::SafeMode: return "SafeMode";
        case StartupState::Faulted:  return "Faulted";
        default:                     return "Unknown";
    }
}

inline const char* startup_stage_to_name(StartupStage st) {
    switch (st) {
        case StartupStage::None:                     return "None";
        case StartupStage::OutputInitialized:        return "OutputInitialized";
        case StartupStage::SourceManagerInitialized: return "SourceManagerInitialized";
        case StartupStage::ConfigStoreInitialized:   return "ConfigStoreInitialized";
        case StartupStage::ConfigLoaded:             return "ConfigLoaded";
        case StartupStage::ConfigValidated:          return "ConfigValidated";
        case StartupStage::OutputsConfigured:        return "OutputsConfigured";
        case StartupStage::LightingStarted:          return "LightingStarted";
        case StartupStage::FactoryStarted:           return "FactoryStarted";
        case StartupStage::NetworkConfigured:        return "NetworkConfigured";
        case StartupStage::ProtocolsStarted:         return "ProtocolsStarted";
        case StartupStage::RuntimeStarted:           return "RuntimeStarted";
        default:                                     return "Unknown";
    }
}

class LumigridRuntime {
public:
    LumigridRuntime();
    ~LumigridRuntime() = default;

    esp_err_t init();
    esp_err_t start();

    StartupState startup_state() const { return startup_state_; }
    const char* startup_state_str() const { return startup_state_to_name(startup_state_); }
    StartupStage startup_stage() const { return startup_stage_; }
    const char* startup_stage_str() const { return startup_stage_to_name(startup_stage_); }
    StartupStage failing_stage() const { return failing_stage_; }
    const char* failing_stage_str() const { return startup_stage_to_name(failing_stage_); }
    esp_err_t failing_error() const { return failing_error_; }

    bool is_safe_mode() const { return safe_mode_ || startup_state_ == StartupState::SafeMode || startup_state_ == StartupState::Faulted; }
    bool is_configuration_valid() const { return configuration_valid_; }
    uint32_t configuration_error_count() const { return configuration_error_count_; }
    const char* configuration_first_error() const { return configuration_first_error_; }

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
    StartupState startup_state_ = StartupState::Booting;
    StartupStage startup_stage_ = StartupStage::None;
    StartupStage failing_stage_ = StartupStage::None;
    esp_err_t failing_error_ = ESP_OK;
    bool configuration_valid_ = true;
    uint32_t configuration_error_count_ = 0;
    char configuration_first_error_[128] = {0};
};

} // namespace lg
