"""Run production runtime shutdown/startup boundary code with failing owners.

The owners stand in for ESP-IDF services. The runtime's real methods decide
whether ownership can be released, Wi-Fi can be stopped, and settings saved.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <atomic>
#include <cinttypes>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_TIMEOUT=0x107, ESP_ERR_INVALID_STATE=0x103,
              ESP_ERR_NOT_FOUND=0x105, ESP_ERR_NVS_NOT_FOUND=0x1102;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(int) {}
const char* esp_err_to_name(int) { return "injected"; }
struct ServiceGuard { explicit ServiceGuard(int) {} };
struct Registry { int removed=0; void unregister_task(const char*) { ++removed; } } registry;
Registry& task_stack_registry() { return registry; }
struct DiagnosticLog {
    static DiagnosticLog& instance() { static DiagnosticLog value; return value; }
    void set_stream_callback(void*) {}
};
enum class StartupStage { None, ProtocolsStarted, LightingStarted, FactoryStarted, ConfigLoaded, NetworkConfigured, ConfigValidated, OutputsConfigured };
enum class StartupState { Ready, Faulted, SafeMode };
enum class RestoreState { None, Interrupted };
enum class RuntimeLifecycle { Constructed, Stopped, Stopping, Running, Faulted };
enum class NetworkApplyState { Idle };
enum class ControlSource { LiveDmx, Factory };
const char* startup_stage_to_name(StartupStage) { return "stage"; }
struct Owner { int result=ESP_OK,calls=0; int stop(int=0) { ++calls; return result; } };
struct FactoryRunner {
    enum class LifecycleState { Stopped, Running, Stopping };
    LifecycleState state=LifecycleState::Stopped;
    bool acknowledge=true;
    LifecycleState lifecycle_state() const { return state; }
    void stop() { state=acknowledge ? LifecycleState::Stopped : LifecycleState::Stopping; }
};
struct DeviceConfig {};
struct Output { bool enabled=true; int blackouts=0,reconfigures=0; void blackout(){++blackouts;} void set_output_enabled(bool e){enabled=e;} int reconfigure(const DeviceConfig&){++reconfigures; return ESP_OK;} };
struct Sources { void release(ControlSource) {} };
struct NetworkSettings { char ap_password[65]{}; unsigned checksum=0; };
unsigned calc_network_settings_crc(const NetworkSettings&) { return 17; }
void generate_random_ap_password(char* p,size_t n) { std::snprintf(p,n,"test-ap-password"); }
struct Store {
    struct ConfigValidationResult { bool valid=true; unsigned error_count=0; char first_error[128]{}; };
    int load_result=ESP_OK,save_result=ESP_OK,writes=0;
    int marker_error=ESP_OK;
    bool restore_active=false;
    NetworkSettings saved{};
    int load_network_settings(NetworkSettings& out) { if(load_result==ESP_OK) out=saved; return load_result; }
    int save_network_settings(const NetworkSettings& in) { ++writes; if(save_result==ESP_OK) saved=in; return save_result; }
    int read_restore_marker(bool& active,uint32_t* checksum){active=restore_active; *checksum=17; return marker_error;}
    int validate_config(const DeviceConfig&,ConfigValidationResult*) { return ESP_OK; }
};
using ConfigStore=Store;
class LumigridRuntime {
public:
    int services_mutex_=0;
    Owner mqtt_,node_sync_,scheduler_,sacn_,artnet_,mdns_,fleet_discovery_,lighting_,api_,wifi_;
    FactoryRunner factory_runner_;
    Output output_;
    Sources source_manager_;
    Store config_;
    struct { bool mqtt=false,node_sync=false,scheduler=false,sacn=false,artnet=false,mdns=false,
                  fleet_discovery=false,lighting=false,factory=false,api=false,wifi=false; } started_subsystems_;
    bool services_started_=true,safe_mode_=false;
    StartupState startup_state_=StartupState::Ready;
    StartupStage startup_stage_=StartupStage::None;
    RuntimeLifecycle lifecycle_state_=RuntimeLifecycle::Running;
    StartupStage failing_stage_=StartupStage::None;
    int failing_error_=ESP_OK;
    RestoreState restore_state_=RestoreState::None;
    bool configuration_valid_=true;
    unsigned configuration_error_count_=0;
    char configuration_first_error_[128]{};
    std::atomic<bool> network_apply_stopping_{false},apply_in_progress_{false};
    std::atomic<NetworkApplyState> apply_state_{NetworkApplyState::Idle};
    __ROLLBACK_DECL__ rollback_services(StartupStage,int);
    __NETWORK_DECL__ stop_network_services();
    esp_err_t stop();
    int prepare_network() {
__NETWORK_SETTINGS__
        return ESP_OK;
    }
    void validate_restore_boot() {
__RESTORE_MARKER__
        DeviceConfig dev_cfg;
__CONFIG_VALIDATION__
    }
};
__ROLLBACK__
__STOP__
__NETWORK_STOP__
int main(int argc,char** argv) {
    assert(argc==2); std::string test=argv[1];
    LumigridRuntime runtime;
    if(test=="restore_marker_read_error" || test=="restore_interrupted" || test=="restore_clear") {
        if(test=="restore_marker_read_error") runtime.config_.marker_error=ESP_FAIL;
        if(test=="restore_interrupted") runtime.config_.restore_active=true;
        runtime.validate_restore_boot();
        if(test=="restore_clear") {
            assert(runtime.configuration_valid_ && runtime.output_.reconfigures==1);
        } else {
            assert(!runtime.configuration_valid_ && runtime.output_.reconfigures==0);
            assert(runtime.safe_mode_ && runtime.startup_state_==StartupState::SafeMode);
            assert(runtime.restore_state_==RestoreState::Interrupted && !runtime.output_.enabled);
            assert(runtime.configuration_first_error_[0]);
        }
    } else if(test=="network_read_error") {
        runtime.config_.load_result=ESP_FAIL;
        assert(runtime.prepare_network()==ESP_FAIL);
        assert(runtime.config_.writes==0);
    } else if(test=="network_save_error") {
        runtime.config_.load_result=ESP_ERR_NOT_FOUND; runtime.config_.save_result=ESP_FAIL;
        assert(runtime.prepare_network()==ESP_FAIL);
        assert(runtime.config_.writes==1);
    } else if(test=="network_first_boot") {
        runtime.config_.load_result=ESP_ERR_NOT_FOUND;
        assert(runtime.prepare_network()==ESP_OK);
        assert(runtime.config_.writes==1 && runtime.config_.saved.ap_password[0]);
    } else if(test=="network_existing") {
        std::strcpy(runtime.config_.saved.ap_password,"existing-password");
        assert(runtime.prepare_network()==ESP_OK && runtime.config_.writes==0);
    } else if(test=="factory_timeout") {
        runtime.started_subsystems_.factory=true;
        runtime.factory_runner_.state=FactoryRunner::LifecycleState::Running;
        runtime.factory_runner_.acknowledge=false;
        assert(runtime.stop()==ESP_ERR_TIMEOUT);
        assert(runtime.started_subsystems_.factory);
        assert(runtime.lifecycle_state_==RuntimeLifecycle::Faulted);
        runtime.factory_runner_.acknowledge=true;
        assert(runtime.stop()==ESP_OK && !runtime.started_subsystems_.factory);
    } else {
        Owner* owner=nullptr; bool* held=nullptr;
        if(test=="lighting_timeout") { owner=&runtime.lighting_; held=&runtime.started_subsystems_.lighting; }
        else if(test=="artnet_timeout") { owner=&runtime.artnet_; held=&runtime.started_subsystems_.artnet; }
        else if(test=="sacn_timeout") { owner=&runtime.sacn_; held=&runtime.started_subsystems_.sacn; }
        else if(test=="sync_timeout") { owner=&runtime.node_sync_; held=&runtime.started_subsystems_.node_sync; }
        else if(test=="mdns_timeout") { owner=&runtime.mdns_; held=&runtime.started_subsystems_.mdns; }
        else if(test=="fleet_timeout") { owner=&runtime.fleet_discovery_; held=&runtime.started_subsystems_.fleet_discovery; }
        else if(test=="scheduler_timeout") { owner=&runtime.scheduler_; held=&runtime.started_subsystems_.scheduler; }
        else if(test=="network_drain_timeout") {
            runtime.started_subsystems_.sacn=true; runtime.sacn_.result=ESP_ERR_TIMEOUT;
            runtime.stop_network_services();
            assert(runtime.started_subsystems_.sacn);
            return 0;
        } else return 2;
        *held=true; owner->result=ESP_ERR_TIMEOUT; runtime.started_subsystems_.wifi=true;
        assert(runtime.stop()==ESP_ERR_TIMEOUT);
        assert(*held && runtime.started_subsystems_.wifi && runtime.wifi_.calls==0);
        assert(!runtime.output_.enabled && runtime.lifecycle_state_==RuntimeLifecycle::Faulted);
        owner->result=ESP_OK;
        assert(runtime.stop()==ESP_OK && !*held && !runtime.started_subsystems_.wifi);
        assert(runtime.lifecycle_state_==RuntimeLifecycle::Stopped);
    }
    std::printf("PASS %s\n",test.c_str());
}
'''

CASES = ["network_read_error", "network_save_error", "network_first_boot", "network_existing",
         "factory_timeout", "lighting_timeout", "artnet_timeout", "sacn_timeout", "sync_timeout",
         "mdns_timeout", "fleet_timeout", "scheduler_timeout", "network_drain_timeout",
         "restore_marker_read_error", "restore_interrupted", "restore_clear"]


def main():
    source = (ROOT / "main/LumigridRuntime.cpp").read_text()
    rollback_type = "esp_err_t" if "esp_err_t LumigridRuntime::rollback_services" in source else "void"
    network_type = "esp_err_t" if "esp_err_t LumigridRuntime::stop_network_services" in source else "void"
    start = source[source.index("esp_err_t LumigridRuntime::start()"):
                   source.index(f"{network_type} LumigridRuntime::stop_network_services")]
    sections = {
        "ROLLBACK_DECL": rollback_type, "NETWORK_DECL": network_type,
        "ROLLBACK": source[source.index(f"{rollback_type} LumigridRuntime::rollback_services"):
                           source.index("esp_err_t LumigridRuntime::stop()")],
        "STOP": source[source.index("esp_err_t LumigridRuntime::stop()"):
                       source.index("esp_err_t LumigridRuntime::start()")],
        "NETWORK_STOP": source[source.index(f"{network_type} LumigridRuntime::stop_network_services"):
                               source.index("esp_err_t LumigridRuntime::start_network_services()")],
        "NETWORK_SETTINGS": start[start.index("  NetworkSettings net_settings;"):
                                  start.index("  const bool factory_setup")],
        "RESTORE_MARKER": source[source.index("  uint32_t restore_chk = 0;"):
                                 source.index("  // Wire recovery and transactional config apply hooks")],
        "CONFIG_VALIDATION": source[source.index("    ConfigStore::ConfigValidationResult val_res;"):
                                    source.index("\n  } else {\n    safe_mode_ = true;\n    startup_state_ = StartupState::SafeMode;")],
    }
    text = HARNESS
    for key, value in sections.items():
        text = text.replace(f"__{key}__", value)
    with tempfile.TemporaryDirectory(prefix="lumigrid-runtime-recovery-") as directory:
        temp = Path(directory)
        (temp / "test.cpp").write_text(text)
        compiler = shutil.which(os.environ.get("CXX", "g++"))
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-fno-pie", "-no-pie", str(temp / "test.cpp"), "-o", str(temp / "test")], check=True)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        failures = [case for case in CASES if subprocess.run([str(temp / "test"), case], env=env).returncode]
        print(f"Runtime recovery: {len(CASES)-len(failures)}/{len(CASES)} passed; failures={failures}")
        return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
