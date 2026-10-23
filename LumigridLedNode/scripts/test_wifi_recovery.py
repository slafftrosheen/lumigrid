"""Exercise the production WifiService state machine with an IDF event/timer fake.

The fake keeps timer callbacks separate from the default event queue so a test
can insert a disconnect while deferred recovery work is pending. It never
models RF reception; physical AP loss/rejoin remains a bench test.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SUPPORT = r'''
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
              ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103,
              ESP_ERR_TIMEOUT = 0x107;
using TickType_t = uint32_t;
using TaskHandle_t = void*;
inline thread_local TaskHandle_t dispatch_task = nullptr;
inline char event_task_identity;
#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    static thread_local char task;
    return dispatch_task ? dispatch_task : &task;
}
inline void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
inline int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline const char* esp_err_to_name(esp_err_t) { return "fake_error"; }
template<class... Args> inline void fake_log(Args&&...) {}
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
#define ESP_RETURN_ON_ERROR(expr, tag, message) do { const int e = (expr); if (e != ESP_OK) return e; } while (0)
using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
using esp_event_handler_instance_t = void*;
#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t const name = #name
inline constexpr char WIFI_EVENT[] = "WIFI_EVENT";
inline constexpr char IP_EVENT[] = "IP_EVENT";
constexpr int ESP_EVENT_ANY_ID = -1;
constexpr int WIFI_EVENT_STA_START = 1, WIFI_EVENT_AP_START = 2,
              WIFI_EVENT_AP_STACONNECTED = 3, WIFI_EVENT_AP_STADISCONNECTED = 4,
              WIFI_EVENT_STA_DISCONNECTED = 5, IP_EVENT_STA_GOT_IP = 6;
struct wifi_event_ap_staconnected_t { uint8_t mac[6]; int aid; };
using wifi_event_ap_stadisconnected_t = wifi_event_ap_staconnected_t;
struct wifi_event_sta_disconnected_t { uint8_t reason; };
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) a[0], a[1], a[2], a[3], a[4], a[5]
struct Registration { esp_event_base_t base; int32_t id; esp_event_handler_t handler; void* arg; };
struct QueuedEvent { esp_event_base_t base; int32_t id; uint32_t data = 0; bool has_data = false; };
inline std::vector<Registration> registrations;
inline std::vector<QueuedEvent> queued;
inline int post_result = ESP_OK;
inline bool auto_pump_barriers = true;
inline void (*post_hook)() = nullptr;
inline void pump();
inline int esp_event_loop_create_default() { return ESP_OK; }
inline int esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    esp_event_handler_t handler, void* arg, esp_event_handler_instance_t* result) {
    registrations.push_back({base, id, handler, arg});
    *result = reinterpret_cast<void*>(registrations.size());
    return ESP_OK;
}
inline int esp_event_post(esp_event_base_t base, int32_t id, const void* data, size_t size, uint32_t) {
    if (!data && post_hook) post_hook();
    if (!data && post_result != ESP_OK) return post_result;
    QueuedEvent event{base, id};
    if (data && size == sizeof(event.data)) {
        std::memcpy(&event.data, data, size);
        event.has_data = true;
    }
    queued.push_back(event);
    // The real default event task runs independently. Deliver a barrier and
    // everything before it in FIFO order while the lifecycle caller waits.
    if (event.has_data && auto_pump_barriers) pump();
    return ESP_OK;
}
inline void emit(esp_event_base_t base, int32_t id, void* data = nullptr) {
    const auto previous = dispatch_task;
    dispatch_task = &event_task_identity;
    auto copy = registrations;
    for (const auto& r : copy)
        if (!std::strcmp(r.base, base) && (r.id == id || r.id == ESP_EVENT_ANY_ID))
            r.handler(r.arg, base, id, data);
    dispatch_task = previous;
}
inline void pump() {
    while (!queued.empty()) {
        auto event = queued.front(); queued.erase(queued.begin());
        emit(event.base, event.id, event.has_data ? &event.data : nullptr);
    }
}
constexpr int ESP_TIMER_TASK = 0;
struct esp_timer_create_args_t {
    void (*callback)(void*); void* arg; int dispatch_method; const char* name; bool skip_unhandled_events;
};
struct FakeTimer { esp_timer_create_args_t args; bool active = false; uint64_t delay = 0; };
using esp_timer_handle_t = FakeTimer*;
inline std::vector<FakeTimer*> timers;
inline int timer_create_result = ESP_OK;
inline int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out) {
    if (timer_create_result != ESP_OK) return timer_create_result;
    *out = new FakeTimer{*args}; timers.push_back(*out); return ESP_OK;
}
inline int esp_timer_start_once(esp_timer_handle_t timer, uint64_t delay) {
    timer->active = true; timer->delay = delay; return ESP_OK;
}
inline int esp_timer_stop(esp_timer_handle_t timer) { timer->active = false; return ESP_OK; }
inline FakeTimer* timer_named(const char* name) {
    for (auto* timer : timers) if (!std::strcmp(timer->args.name, name)) return timer;
    return nullptr;
}
inline void fire(const char* name) {
    auto* timer = timer_named(name);
    if (timer && timer->active) { timer->active = false; timer->args.callback(timer->args.arg); }
}
enum wifi_mode_t { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA };
constexpr int WIFI_IF_STA = 0, WIFI_IF_AP = 1, WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3;
struct wifi_sta_config_t { uint8_t ssid[32]; uint8_t password[64]; };
struct wifi_ap_config_t { uint8_t ssid[32]; uint8_t password[64]; uint8_t ssid_len; uint8_t channel; uint8_t max_connection; uint8_t ssid_hidden; int authmode; };
struct wifi_config_t { wifi_sta_config_t sta; wifi_ap_config_t ap; };
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_ap_record_t { int8_t rssi; };
struct esp_ip4_addr_t { uint32_t addr; };
struct esp_netif_ip_info_t { esp_ip4_addr_t ip; };
struct esp_netif_t {};
inline wifi_mode_t hardware_mode = WIFI_MODE_NULL;
inline int connect_result = ESP_OK, connect_calls = 0, mode_result = ESP_OK;
inline bool inline_ready_on_start = false;
inline wifi_config_t last_sta_config{}, last_ap_config{};
inline int esp_wifi_init(const wifi_init_config_t*) { return ESP_OK; }
inline int esp_wifi_set_mode(wifi_mode_t mode) {
    if (mode_result != ESP_OK) return mode_result;
    hardware_mode = mode; return ESP_OK;
}
inline int esp_wifi_get_mode(wifi_mode_t* mode) { *mode = hardware_mode; return ESP_OK; }
inline int esp_wifi_set_config(int interface, const wifi_config_t* config) {
    (interface == WIFI_IF_STA ? last_sta_config : last_ap_config) = *config; return ESP_OK;
}
inline int esp_wifi_start() {
    if (inline_ready_on_start) {
        emit(WIFI_EVENT, WIFI_EVENT_STA_START);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
    }
    return ESP_OK;
}
inline int esp_wifi_stop() { return ESP_OK; }
inline int esp_wifi_connect() { ++connect_calls; return connect_result; }
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t* record) { record->rssi = -45; return ESP_OK; }
inline int esp_netif_init() { return ESP_OK; }
inline esp_netif_t* esp_netif_create_default_wifi_sta() { static esp_netif_t value; return &value; }
inline esp_netif_t* esp_netif_create_default_wifi_ap() { static esp_netif_t value; return &value; }
inline int esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t* info) { info->ip.addr = 1; return ESP_OK; }
namespace lg { struct NvsRuntime { static bool ready() { return true; } static int initialise() { return ESP_OK; } }; }
'''

HARNESS = r'''
#include "support.hpp"
#include "WifiService.hpp"
#include <cassert>
#include <iostream>
using namespace lg;

// Keep the blocking state outside the callback object: the old implementation
// incorrectly destroys that object while it is still running.
struct CallbackBlocker {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
    void block() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return released; });
    }
    void wait_until_entered() {
        std::unique_lock<std::mutex> lock(mutex);
        assert(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        condition.notify_all();
    }
} blocker;

WifiConfig fallback_config() {
    WifiConfig config;
    config.mode = WifiMode::StaWithApFallback;
    config.sta_ssid = "test-router";
    config.ap_ssid = "LumiGrid-Recovery";
    config.ap_password = "test-only-password";
    config.max_retry = 0;
    return config;
}
void disconnect() {
    wifi_event_sta_disconnected_t event{201};
    emit(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
}
void enter_fallback(WifiService& wifi, std::vector<WifiState>& states) {
    assert(wifi.start(fallback_config(), [&](WifiState state) { states.push_back(state); }) == ESP_OK);
    disconnect();
    assert(wifi.is_fallback_ap() && hardware_mode == WIFI_MODE_APSTA);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string name = argv[1];
    WifiService wifi;
    std::vector<WifiState> states;
    if (name == "disconnect_before_exit" || name == "disconnect_queued_exit") {
        enter_fallback(wifi, states);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        assert(wifi.state() == WifiState::Connected);
        if (name == "disconnect_queued_exit") {
            fire("wifi_fb_exit");
            assert(hardware_mode == WIFI_MODE_APSTA); // timer must only enqueue
        }
        disconnect();
        assert(wifi.state() == WifiState::FallbackAp && !wifi.status().sta_connected);
        assert(states.back() == WifiState::FallbackAp);
        if (name == "disconnect_before_exit") fire("wifi_fb_exit");
        pump();
        assert(wifi.is_fallback_ap() && hardware_mode == WIFI_MODE_APSTA && wifi.has_ip());
        assert(timer_named("wifi_fb_timer")->active);
    } else if (name == "retry_after_sync_failure") {
        enter_fallback(wifi, states);
        connect_result = ESP_FAIL;
        fire("wifi_fb_timer"); pump();
        assert(wifi.diagnostics().reconnect_failures == 1);
        assert(timer_named("wifi_fb_timer")->active);
        connect_result = ESP_OK;
        fire("wifi_fb_timer"); pump();
        assert(connect_calls == 2);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        fire("wifi_fb_exit"); pump();
        assert(!wifi.is_fallback_ap() && hardware_mode == WIFI_MODE_STA);
    } else if (name == "stopped_ignores_late_events") {
        enter_fallback(wifi, states);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        fire("wifi_fb_exit");
        assert(wifi.stop() == ESP_OK);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        emit(WIFI_EVENT, WIFI_EVENT_AP_START);
        pump();
        assert(wifi.state() == WifiState::Idle && !wifi.has_ip());
    } else if (name == "deferred_exit_requires_active_connection") {
        wifi.schedule_exit_fallback_ap();
        fire("wifi_fb_exit"); pump();
        assert(hardware_mode == WIFI_MODE_NULL && timers.empty());
    } else if (name == "ready_before_start_returns") {
        inline_ready_on_start = true;
        assert(wifi.start(fallback_config(), {}) == ESP_OK);
        assert(wifi.state() == WifiState::Connected && wifi.has_ip());
    } else if (name == "healthy_exit") {
        enter_fallback(wifi, states);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        fire("wifi_fb_exit"); pump();
        assert(wifi.state() == WifiState::Connected && !wifi.is_fallback_ap());
        assert(hardware_mode == WIFI_MODE_STA && wifi.diagnostics().fallback_exit_attempts == 1);
    } else if (name == "full_length_ssid") {
        auto config = fallback_config();
        config.sta_ssid = std::string(32, 'S');
        config.ap_ssid = std::string(40, 'A');
        config.sta_password = std::string(64, 'a');
        assert(wifi.start(config, {}) == ESP_OK);
        assert(last_sta_config.sta.ssid[31] == 'S');
        assert(last_sta_config.sta.password[63] == 'a');
        disconnect();
        assert(last_ap_config.ap.ssid_len == 32);
        assert(last_ap_config.ap.ssid[31] == 'A');
    } else if (name == "event_queue_pressure") {
        enter_fallback(wifi, states);
        post_result = ESP_FAIL;
        fire("wifi_fb_timer");
        assert(timer_named("wifi_fb_timer")->active && connect_calls == 0);
        post_result = ESP_OK;
        fire("wifi_fb_timer"); pump();
        assert(connect_calls == 1);
    } else if (name == "stale_retry_after_got_ip") {
        enter_fallback(wifi, states);
        fire("wifi_fb_timer");
        assert(connect_calls == 0);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        pump();
        assert(connect_calls == 0 && wifi.state() == WifiState::Connected);
    } else if (name == "ap_reconfiguration_ignores_sta_events") {
        enter_fallback(wifi, states);
        assert(wifi.stop() == ESP_OK);
        auto config = fallback_config();
        config.mode = WifiMode::ApOnly;
        assert(wifi.start(config, {}) == ESP_OK);
        emit(WIFI_EVENT, WIFI_EVENT_AP_START);
        assert(wifi.state() == WifiState::ApActive);
        emit(WIFI_EVENT, WIFI_EVENT_STA_START);
        disconnect();
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        assert(wifi.state() == WifiState::ApActive && wifi.has_ip() && connect_calls == 0);
    } else if (name == "sta_reconfiguration_ignores_ap_event") {
        enter_fallback(wifi, states);
        assert(wifi.stop() == ESP_OK);
        auto config = fallback_config();
        config.mode = WifiMode::StaOnly;
        assert(wifi.start(config, {}) == ESP_OK);
        emit(WIFI_EVENT, WIFI_EVENT_AP_START);
        assert(wifi.state() == WifiState::Connecting && !wifi.has_ip());
    } else if (name == "startup_sync_failure_falls_back" ||
               name == "retry_sync_failure_falls_back" ||
               name == "sta_only_sync_failure_is_reported") {
        auto config = fallback_config();
        config.max_retry = 3;
        if (name == "sta_only_sync_failure_is_reported") config.mode = WifiMode::StaOnly;
        assert(wifi.start(config, [&](WifiState state) { states.push_back(state); }) == ESP_OK);
        connect_result = ESP_FAIL;
        if (name == "retry_sync_failure_falls_back") disconnect();
        else emit(WIFI_EVENT, WIFI_EVENT_STA_START);
        assert(wifi.diagnostics().reconnect_failures == 1);
        if (config.mode == WifiMode::StaOnly) {
            assert(wifi.state() == WifiState::Failed && !wifi.has_ip());
            assert(!states.empty() && states.back() == WifiState::Failed);
        } else {
            assert(wifi.is_fallback_ap() && wifi.state() == WifiState::FallbackAp && wifi.has_ip());
            assert(timer_named("wifi_fb_timer") && timer_named("wifi_fb_timer")->active);
            connect_result = ESP_OK;
            fire("wifi_fb_timer"); pump();
            assert(connect_calls == 2);
        }
    } else if (name == "fallback_start_failure_is_reported") {
        assert(wifi.start(fallback_config(), [&](WifiState state) { states.push_back(state); }) == ESP_OK);
        mode_result = ESP_FAIL;
        disconnect();
        assert(wifi.state() == WifiState::Failed && !wifi.has_ip());
        assert(!states.empty() && states.back() == WifiState::Failed);
    } else if (name == "stop_waits_for_callback") {
        assert(wifi.start(fallback_config(), [](WifiState) { blocker.block(); }) == ESP_OK);
        std::thread event_thread([] { emit(IP_EVENT, IP_EVENT_STA_GOT_IP); });
        blocker.wait_until_entered();
        auto stopped = std::async(std::launch::async, [&] { return wifi.stop(); });
        const bool returned_while_callback_running =
            stopped.wait_for(std::chrono::milliseconds(30)) == std::future_status::ready;
        blocker.release();
        event_thread.join();
        assert(stopped.get() == ESP_OK);
        assert(!returned_while_callback_running);
        assert(wifi.state() == WifiState::Idle && !wifi.has_ip());
    } else if (name == "stop_timeout_retains_callback") {
        auto lifetime = std::make_shared<int>(1);
        std::weak_ptr<int> weak_lifetime = lifetime;
        assert(wifi.start(fallback_config(), [lifetime](WifiState) { blocker.block(); }) == ESP_OK);
        lifetime.reset();
        std::thread event_thread([] { emit(IP_EVENT, IP_EVENT_STA_GOT_IP); });
        blocker.wait_until_entered();
        const auto began = std::chrono::steady_clock::now();
        const esp_err_t err = wifi.stop();
        const auto elapsed = std::chrono::steady_clock::now() - began;
        const bool retained = !weak_lifetime.expired();
        blocker.release();
        event_thread.join();
        assert(err == ESP_ERR_TIMEOUT && retained);
        assert(elapsed < std::chrono::seconds(3));
        assert(wifi.stop() == ESP_OK);
        assert(weak_lifetime.expired());
        assert(wifi.state() == WifiState::Idle);
    } else if (name == "reentrant_lifecycle_is_rejected") {
        esp_err_t stop_result = ESP_OK, start_result = ESP_OK;
        assert(wifi.start(fallback_config(), [&](WifiState) {
            stop_result = wifi.stop();
            start_result = wifi.start(fallback_config(), {});
        }) == ESP_OK);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        assert(stop_result == ESP_ERR_INVALID_STATE && start_result == ESP_ERR_INVALID_STATE);
        assert(wifi.state() == WifiState::Connected && wifi.has_ip());
    } else if (name == "retry_timer_allocation_failure_is_reported") {
        timer_create_result = ESP_ERR_NO_MEM;
        assert(wifi.start(fallback_config(), {}) == ESP_ERR_NO_MEM);
        assert(wifi.diagnostics().last_error == ESP_ERR_NO_MEM);
        timer_create_result = ESP_OK;
        assert(wifi.start(fallback_config(), {}) == ESP_OK);
        disconnect();
        assert(wifi.is_fallback_ap() && timer_named("wifi_fb_timer")->active);
    } else if (name == "stop_cancels_timer_rearmed_by_entered_callback") {
        enter_fallback(wifi, states);
        post_result = ESP_FAIL;
        post_hook = [] { blocker.block(); };
        std::thread timer_thread([] { fire("wifi_fb_timer"); });
        blocker.wait_until_entered();
        auto stopped = std::async(std::launch::async, [&] { return wifi.stop(); });
        const bool returned_while_timer_running =
            stopped.wait_for(std::chrono::milliseconds(30)) == std::future_status::ready;
        blocker.release();
        timer_thread.join();
        assert(stopped.get() == ESP_OK && !returned_while_timer_running);
        assert(!timer_named("wifi_fb_timer")->active);
        assert(wifi.state() == WifiState::Idle);
    } else if (name == "sta_reconfiguration_drops_queued_old_ip") {
        assert(wifi.start(fallback_config(), {}) == ESP_OK);
        queued.push_back({IP_EVENT, IP_EVENT_STA_GOT_IP});
        assert(wifi.stop() == ESP_OK);
        auto next_config = fallback_config();
        next_config.sta_ssid = "different-router";
        assert(wifi.start(next_config, {}) == ESP_OK);
        pump();
        assert(wifi.state() == WifiState::Connecting && !wifi.has_ip());
    } else if (name == "start_event_queue_barrier_timeout") {
        auto_pump_barriers = false;
        assert(wifi.start(fallback_config(), {}) == ESP_ERR_TIMEOUT);
        assert(wifi.is_intentional_disconnect() && !wifi.has_ip());
        auto_pump_barriers = true;
        assert(wifi.stop() == ESP_OK);
        assert(wifi.start(fallback_config(), {}) == ESP_OK);
    } else if (name == "stop_event_queue_barrier_timeout_retains_callback") {
        auto lifetime = std::make_shared<int>(1);
        std::weak_ptr<int> weak_lifetime = lifetime;
        assert(wifi.start(fallback_config(), [lifetime](WifiState) {}) == ESP_OK);
        lifetime.reset();
        auto_pump_barriers = false;
        assert(wifi.stop() == ESP_ERR_TIMEOUT);
        assert(!weak_lifetime.expired());
        auto_pump_barriers = true;
        assert(wifi.stop() == ESP_OK);
        assert(weak_lifetime.expired());
    } else if (name == "lifecycle_from_event_loop_is_rejected") {
        assert(wifi.start(fallback_config(), {}) == ESP_OK);
        esp_event_handler_instance_t other_handler;
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            [](void* context, esp_event_base_t, int32_t, void*) {
                auto& service = *static_cast<WifiService*>(context);
                assert(service.stop() == ESP_ERR_INVALID_STATE);
                assert(service.start(fallback_config(), {}) == ESP_ERR_INVALID_STATE);
            }, &wifi, &other_handler);
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
        assert(wifi.state() == WifiState::Connected);
    } else {
        assert(false && "Unknown test");
    }
    std::cout << "PASS " << name << '\n';
}
'''


def main():
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        raise SystemExit("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-wifi-recovery-") as directory:
        build = Path(directory)
        (build / "support.hpp").write_text(SUPPORT)
        for name in ("esp_check.h", "esp_event.h", "esp_log.h", "esp_mac.h",
                     "esp_netif.h", "esp_wifi.h", "esp_err.h", "esp_timer.h",
                     "esp_netif_types.h", "esp_wifi_types.h", "NvsRuntime.hpp"):
            (build / name).write_text('#include "support.hpp"\n')
        (build / "freertos").mkdir()
        for name in ("FreeRTOS.h", "task.h"):
            (build / "freertos" / name).write_text('#include "support.hpp"\n')
        (build / "harness.cpp").write_text(HARNESS)
        executable = build / "wifi_recovery"
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-pthread",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                        "-I", str(build), "-I", str(ROOT / "components/network/include"),
                        str(ROOT / "components/network/WifiService.cpp"),
                        str(build / "harness.cpp"), "-o", str(executable)], check=True)
        cases = ("disconnect_before_exit", "disconnect_queued_exit", "retry_after_sync_failure",
                 "stopped_ignores_late_events", "deferred_exit_requires_active_connection",
                 "ready_before_start_returns", "healthy_exit", "full_length_ssid",
                 "event_queue_pressure", "stale_retry_after_got_ip",
                 "ap_reconfiguration_ignores_sta_events", "sta_reconfiguration_ignores_ap_event",
                 "startup_sync_failure_falls_back", "retry_sync_failure_falls_back",
                 "sta_only_sync_failure_is_reported", "fallback_start_failure_is_reported",
                 "stop_waits_for_callback", "stop_timeout_retains_callback",
                 "reentrant_lifecycle_is_rejected", "retry_timer_allocation_failure_is_reported",
                 "stop_cancels_timer_rearmed_by_entered_callback",
                 "sta_reconfiguration_drops_queued_old_ip", "start_event_queue_barrier_timeout",
                 "stop_event_queue_barrier_timeout_retains_callback", "lifecycle_from_event_loop_is_rejected")
        failures = []
        for case in cases:
            env = os.environ.copy()
            # Production timers/registrations have static runtime lifetime.
            env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            result = subprocess.run([str(executable), case], capture_output=True, text=True,
                                    timeout=10, env=env)
            print(result.stdout or result.stderr, end="")
            if result.returncode:
                failures.append(case)
        if failures:
            raise SystemExit(f"{len(failures)}/{len(cases)} Wi-Fi cases failed: {', '.join(failures)}")
        print(f"PASS: {len(cases)} production Wi-Fi recovery scenarios")


if __name__ == "__main__":
    main()
