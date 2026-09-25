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
#include "CueRunner.hpp"
#include "ShowTransport.hpp"
#include "MqttService.hpp"
#include "FactoryRunner.hpp"
#include "FactoryApi.hpp"
#include "AuthStore.hpp"
#include "AuthManager.hpp"
#include "FleetDiscoveryService.hpp"
#include "esp_err.h"
#include <atomic>

namespace lg {

enum class RuntimeLifecycle : uint8_t {
    Constructed,
    Initializing,
    NetworkReady,
    ServicesStarting,
    Running,
    Stopping,
    Stopped,
    Faulted
};

inline const char* runtime_lifecycle_to_name(RuntimeLifecycle st) {
    switch (st) {
        case RuntimeLifecycle::Constructed:      return "Constructed";
        case RuntimeLifecycle::Initializing:     return "Initializing";
        case RuntimeLifecycle::NetworkReady:     return "NetworkReady";
        case RuntimeLifecycle::ServicesStarting: return "ServicesStarting";
        case RuntimeLifecycle::Running:          return "Running";
        case RuntimeLifecycle::Stopping:         return "Stopping";
        case RuntimeLifecycle::Stopped:          return "Stopped";
        case RuntimeLifecycle::Faulted:          return "Faulted";
        default:                                 return "Unknown";
    }
}

enum class StartupState : uint8_t {
    Booting,
    StartingNetwork,
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
        case StartupState::Booting:         return "Booting";
        case StartupState::StartingNetwork: return "StartingNetwork";
        case StartupState::Ready:           return "Ready";
        case StartupState::SafeMode:        return "SafeMode";
        case StartupState::Faulted:         return "Faulted";
        default:                            return "Unknown";
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
    esp_err_t stop();

    RuntimeLifecycle lifecycle_state() const { return lifecycle_state_; }
    const char* lifecycle_state_str() const { return runtime_lifecycle_to_name(lifecycle_state_); }

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

    OutputController& output() { return output_; }
    LightingRuntime& lighting() { return lighting_; }
    CueRunner& cue_runner() { return cue_runner_; }
    ShowTransportCoordinator& show_transport() { return show_transport_; }
    FactoryRunner& factory_runner() { return factory_runner_; }
    DmxRouter& dmx_router() { return dmx_router_; }
    ConfigStore& config() { return config_; }
    auth::AuthManager& auth_manager() { return auth_manager_; }
    discovery::FleetDiscoveryService& fleet_discovery() { return fleet_discovery_; }

    esp_err_t queue_network_apply(const NetworkApplyRequest& request);
    NetworkApplyState network_apply_state() const { return apply_state_.load(); }
    uint32_t pending_apply_command_id() const { return pending_apply_command_id_.load(); }
    const char* last_apply_error() const { return last_apply_error_; }
    bool is_network_apply_in_progress() const { return apply_in_progress_.load(); }

private:
    static void recovery_task(void* arg);
    static void net_apply_worker(void* arg);
    void execute_network_apply(const NetworkApplyRequest& req);
    void stop_network_services();
    void start_network_services();
    void rollback_services(StartupStage failed_stage, esp_err_t error);

    OutputController output_;
    ControlSourceManager source_manager_;
    ConfigStore config_;
    auth::AuthStore auth_store_;
    auth::AuthManager auth_manager_{&auth_store_};
    FactoryRunner factory_runner_;
    FactoryApi api_;
    WifiService wifi_;
    MdnsService mdns_;
    ArtnetDiscovery artnet_;
    SacnReceiver sacn_;
    LightingRuntime lighting_;
    CueRunner cue_runner_;
    ShowTransportCoordinator show_transport_;
    DmxRouter dmx_router_;
    ClockDiscipline clock_;
    Scheduler scheduler_;
    NodeSync node_sync_;
    MqttService mqtt_;
    discovery::FleetDiscoveryService fleet_discovery_;

    struct StartedSubsystems {
        bool wifi = false;
        bool api = false;
        bool mdns = false;
        bool fleet_discovery = false;
        bool artnet = false;
        bool sacn = false;
        bool scheduler = false;
        bool node_sync = false;
        bool mqtt = false;
        bool lighting = false;
        bool factory = false;
    } started_subsystems_{};

    RuntimeLifecycle lifecycle_state_ = RuntimeLifecycle::Constructed;
    bool services_started_ = false;
    // Semaphore signalled by the WiFi callback when network is ready.
    // Declared atomic so the event-loop task always observes the correct
    // pointer with acquire/release ordering (no data race with creator task).
    std::atomic<SemaphoreHandle_t> net_ready_sem_{nullptr};

    bool safe_mode_ = false;
    StartupState startup_state_ = StartupState::Booting;
    StartupStage startup_stage_ = StartupStage::None;
    StartupStage failing_stage_ = StartupStage::None;
    esp_err_t failing_error_ = ESP_OK;
    bool configuration_valid_ = true;
    uint32_t configuration_error_count_ = 0;
    char configuration_first_error_[128] = {0};

    QueueHandle_t net_apply_queue_ = nullptr;
    TaskHandle_t net_apply_task_ = nullptr;
    std::atomic<bool> apply_in_progress_{false};
    std::atomic<NetworkApplyState> apply_state_{NetworkApplyState::Idle};
    std::atomic<uint32_t> pending_apply_command_id_{0};
    const char* last_apply_error_ = nullptr;
};

} // namespace lg
