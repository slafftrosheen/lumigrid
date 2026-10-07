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
#include "RecoveryTypes.hpp"
#include "ConfigApplyPlan.hpp"
#include "OtaBootVerifier.hpp"
#include "CertificationService.hpp"
#include "RuntimeServices.hpp"
#include "RuntimeMemory.hpp"
#include "esp_err.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


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
    CertificationService& certification() { return cert_service_; }
    const CertificationService& certification() const { return cert_service_; }

    esp_err_t queue_network_apply(const NetworkApplyRequest& request);
    NetworkApplyState network_apply_state() const { return apply_state_.load(); }
    uint32_t pending_apply_command_id() const { return pending_apply_command_id_.load(); }
    const char* last_apply_error() const { return last_apply_error_; }
    bool is_network_apply_in_progress() const { return apply_in_progress_.load(); }

    // LG-038.3 §60: active service state for diagnostics. Reflects the last
    // reconciled plan plus actual started_subsystems_ truth.
    RuntimeServicePlan service_plan() const;

    // LG-038.3 §61-65: deterministic plan for the current state.

    RecoveryStatus get_recovery_status() const;
    esp_err_t apply_device_config(const DeviceConfig& old_cfg, const DeviceConfig& new_cfg, const ConfigApplyPlan& plan);

    MaintenanceMode maintenance_mode() const { return maintenance_mode_.load(std::memory_order_acquire); }
    void set_maintenance_mode(MaintenanceMode m) { maintenance_mode_.store(m, std::memory_order_release); }

    RestoreState restore_state() const { return restore_state_; }
    void set_restore_state(RestoreState s) { restore_state_ = s; }

    int boot_count() const { return boot_count_; }
    void request_service_reconcile();

private:
    void write_serial_diagnostics();

    static void recovery_task(void* arg);
    static void net_apply_worker(void* arg);
    void execute_network_apply(const NetworkApplyRequest& req);
    void stop_network_services();
    esp_err_t start_network_services();
    void rollback_services(StartupStage failed_stage, esp_err_t error);

    // LG-038.3 §7/§8/§10: on-demand network apply worker. One owned pending
    // request (apply_in_progress_ already serializes applies); the 8 KB task
    // exists only while an apply runs, then deletes itself.
    esp_err_t start_network_apply_task();

    // LG-038.3 §64/§65: plan reconciliation for config and network changes.
    esp_err_t reconcile_services(const DeviceConfig& old_cfg, const DeviceConfig& new_cfg,
                                 bool network_changed);
    esp_err_t start_artnet_service(const DeviceConfig& cfg);
    esp_err_t start_sacn_service(const DeviceConfig& cfg);

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
    CertificationService cert_service_;

    struct StartedSubsystems {
        std::atomic<bool> wifi = false;
        std::atomic<bool> api = false;
        std::atomic<bool> mdns = false;
        std::atomic<bool> fleet_discovery = false;
        std::atomic<bool> artnet = false;
        std::atomic<bool> sacn = false;
        std::atomic<bool> scheduler = false;
        std::atomic<bool> node_sync = false;
        std::atomic<bool> mqtt = false;
        std::atomic<bool> lighting = false;
        std::atomic<bool> factory = false;
    } started_subsystems_{};

    const StartedSubsystems& started_subsystems() const { return started_subsystems_; }
    NetworkPlanState network_plan_state() const { return network_plan_state_.load(std::memory_order_acquire); }
    void set_network_plan_state(NetworkPlanState s) { network_plan_state_.store(s, std::memory_order_release); }
    CommissioningPlanState commissioning_plan_state() const;

    RuntimeLifecycle lifecycle_state_ = RuntimeLifecycle::Constructed;
    std::atomic<bool> services_started_{false};
    SemaphoreHandle_t services_mutex_ = nullptr;
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

    std::atomic<TaskHandle_t> net_apply_task_{nullptr};
    std::atomic<bool> network_apply_stopping_{false};
    // LG-038.3 §10: single owned pending request instead of a permanent
    // queue + worker. apply_in_progress_ still serializes applies.
    NetworkApplyRequest pending_apply_{};
    std::atomic<bool> apply_in_progress_{false};
    std::atomic<NetworkApplyState> apply_state_{NetworkApplyState::Idle};
    std::atomic<uint32_t> pending_apply_command_id_{0};
    const char* last_apply_error_ = nullptr;

    // Network state observed by the service planner.
    std::atomic<NetworkPlanState> network_plan_state_{NetworkPlanState::NoNetwork};

    std::atomic<MaintenanceMode> maintenance_mode_{MaintenanceMode::None};
    RestoreState restore_state_ = RestoreState::Idle;
    char recovery_reason_[64] = {0};
    int boot_count_ = 0;
    std::atomic<bool> service_reconcile_pending_{false};
    std::atomic<bool> reconcile_worker_running_{false};
};


} // namespace lg
