#!/usr/bin/env python3
"""Exercise production output drivers and controller on the host.

Usage: python3 scripts/test_output_drivers.py [--no-sanitize] [--case CASE]
The real driver, controller, board, configuration, and memory-admission code is
compiled with runtime test mode disabled. A second build covers the SDK's
PM-enabled partial-deletion quarantine policy. Only ESP-IDF/FreeRTOS boundary headers
and the executor's task dispatch are substituted. Recording transports model
PCA registers and the RMT channel-owned completion ledger; no hardware is used.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap


HOST_HEADERS = {
    "sdkconfig.h": "#define CONFIG_RMT_ISR_IRAM_SAFE 1\n",
    "esp_attr.h": "#define IRAM_ATTR\n#define DRAM_ATTR\n",
    "esp_err.h": """
        #include <cstddef>
        #include <cstdint>
        using esp_err_t = int;
        constexpr int ESP_OK = 0, ESP_FAIL = -1;
        constexpr int ESP_ERR_NO_MEM = 0x101, ESP_ERR_INVALID_ARG = 0x102;
        constexpr int ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_INVALID_SIZE = 0x104;
        constexpr int ESP_ERR_NOT_FOUND = 0x105, ESP_ERR_NOT_SUPPORTED = 0x106;
        constexpr int ESP_ERR_TIMEOUT = 0x107, ESP_ERR_INVALID_RESPONSE = 0x108;
        inline const char* esp_err_to_name(int) { return "host-error"; }
    """,
    "esp_log.h": """
        template<class... Args> inline void host_log(Args&&...) {}
        #define ESP_LOGI(...) host_log(__VA_ARGS__)
        #define ESP_LOGW(...) host_log(__VA_ARGS__)
        #define ESP_LOGE(...) host_log(__VA_ARGS__)
    """,
    "esp_check.h": """
        #define ESP_RETURN_ON_ERROR(expr, ...) do { \
            const auto host_err = (expr); if (host_err != ESP_OK) return host_err; \
        } while (0)
    """,
    "esp_rom_sys.h": "inline void esp_rom_delay_us(unsigned) {}\n",
    "esp_timer.h": """
        #include <cstdint>
        #include <atomic>
        inline int64_t esp_timer_get_time() { static std::atomic<int64_t> now{0}; return ++now; }
    """,
    "esp_heap_caps.h": """
        #include <cstdlib>
        constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_DMA = 4;
        inline void* heap_caps_calloc(size_t n, size_t size, unsigned) { return calloc(n, size); }
        inline void heap_caps_free(void* ptr) { free(ptr); }
        inline size_t heap_caps_get_free_size(unsigned) { return 1024 * 1024; }
        inline size_t heap_caps_get_largest_free_block(unsigned) { return 1024 * 1024; }
    """,
    "esp_system.h": "inline unsigned esp_get_minimum_free_heap_size() { return 1024 * 1024; }\n",
    # These SDK declarations are needed by real ConfigStore/NetworkSettings
    # headers, but their persistence/network functions are not linked or called.
    "nvs.h": "using nvs_handle_t = unsigned;\n",
    "nvs_flash.h": "",
    "esp_rom_crc.h": "#include <cstdint>\nuint32_t esp_rom_crc32_le(uint32_t, const uint8_t*, uint32_t);\n",
    "esp_random.h": "#include <cstddef>\nvoid esp_fill_random(void*, size_t);\n",
    "freertos/FreeRTOS.h": """
        #include <cstdint>
        using TickType_t = unsigned;
        using BaseType_t = int;
        using portMUX_TYPE = int;
        constexpr unsigned portMAX_DELAY = ~0u;
        constexpr int pdTRUE = 1, pdFALSE = 0;
        #define pdMS_TO_TICKS(x) (x)
        #define portMUX_INITIALIZER_UNLOCKED 0
        inline void portENTER_CRITICAL(portMUX_TYPE*) {}
        inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
    """,
    "freertos/task.h": """
        #include "FreeRTOS.h"
        using TaskHandle_t = void*;
        inline int xPortGetCoreID() { return 1; }
        inline void vTaskDelay(TickType_t) {}
        inline TaskHandle_t xTaskGetHandle(const char*) { return nullptr; }
    """,
    "freertos/queue.h": "using QueueHandle_t = void*;\n",
    "freertos/semphr.h": """
        #include "FreeRTOS.h"
        #include <mutex>
        #include <new>
        using SemaphoreHandle_t = std::recursive_mutex*;
        inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new (std::nothrow) std::recursive_mutex; }
        inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return xSemaphoreCreateMutex(); }
        inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) {
            if (!mutex) return pdFALSE;
            if (!timeout) return mutex->try_lock() ? pdTRUE : pdFALSE;
            mutex->lock(); return pdTRUE;
        }
        inline int xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); return pdTRUE; }
        inline int xSemaphoreTakeRecursive(SemaphoreHandle_t m, TickType_t t) { return xSemaphoreTake(m, t); }
        inline int xSemaphoreGiveRecursive(SemaphoreHandle_t m) { return xSemaphoreGive(m); }
        inline void vSemaphoreDelete(SemaphoreHandle_t mutex) { delete mutex; }
    """,
    "driver/gpio.h": """
        #include "esp_err.h"
        enum gpio_num_t { GPIO_ENUM_VALUES };
        enum { GPIO_MODE_OUTPUT, GPIO_MODE_INPUT_OUTPUT, GPIO_MODE_INPUT_OUTPUT_OD, GPIO_PULLUP_ENABLE,
               GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE };
        struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
        inline int host_gpio_levels[40]{};
        namespace host_gpio {
            inline int fail_pin = -1, fail_level = -1, config_fail_pin = -1;
            inline unsigned failures = 0, config_failures = 0, configurations = 0;
            inline int held_low_pin = -1;
            inline unsigned held_low_after_reads = 1, selected_reads = 0;
            inline void reset_faults() {
                fail_pin = fail_level = config_fail_pin = held_low_pin = -1;
                failures = config_failures = selected_reads = 0;
                held_low_after_reads = 1;
            }
        }
        inline int gpio_set_level(gpio_num_t pin, int value) {
            if (host_gpio::failures && pin == host_gpio::fail_pin &&
                (host_gpio::fail_level < 0 || value == host_gpio::fail_level)) {
                --host_gpio::failures; return ESP_FAIL;
            }
            host_gpio_levels[pin] = value; return ESP_OK;
        }
        inline int gpio_get_level(gpio_num_t pin) {
            if (pin == host_gpio::held_low_pin &&
                ++host_gpio::selected_reads >= host_gpio::held_low_after_reads) return 0;
            return host_gpio_levels[pin];
        }
        inline int gpio_config(const gpio_config_t* cfg) {
            ++host_gpio::configurations;
            if (host_gpio::config_failures && host_gpio::config_fail_pin >= 0 &&
                (cfg->pin_bit_mask & (1ULL << host_gpio::config_fail_pin))) {
                --host_gpio::config_failures; return ESP_FAIL;
            }
            return ESP_OK;
        }
        inline int gpio_reset_pin(gpio_num_t) { return ESP_OK; }
        inline int gpio_set_direction(gpio_num_t, int) { return ESP_OK; }
    """,
    # SDK transport stubs fail closed until explicitly enabled. Most cases use
    # recording transports; production transport cases enable these boundaries
    # to exercise I2C recovery and RMT lifecycle/queue policy. No timing is emulated.
    "driver/i2c.h": """
        #include "gpio.h"
        #include <array>
        #include <algorithm>
        using i2c_port_t = int;
        constexpr int I2C_NUM_0 = 0, I2C_MODE_MASTER = 1;
        struct i2c_config_t {
            int mode; gpio_num_t sda_io_num, scl_io_num; int sda_pullup_en, scl_pullup_en;
            struct { unsigned clk_speed; } master; unsigned clk_flags;
        };
        namespace host_i2c {
            inline bool active = false, live = false;
            inline unsigned installs = 0, deletes = 0, delete_failures = 0;
            inline std::array<uint8_t, 256> registers{};
        }
        inline int i2c_param_config(int, const i2c_config_t*) {
            return host_i2c::active ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
        }
        inline int i2c_driver_install(int, int, int, int, int) {
            if (!host_i2c::active) return ESP_ERR_NOT_SUPPORTED;
            if (host_i2c::live) return ESP_ERR_INVALID_STATE;
            host_i2c::live = true; ++host_i2c::installs; return ESP_OK;
        }
        inline int i2c_driver_delete(int) {
            if (!host_i2c::active) return ESP_ERR_NOT_SUPPORTED;
            ++host_i2c::deletes;
            if (host_i2c::delete_failures) { --host_i2c::delete_failures; return ESP_FAIL; }
            if (!host_i2c::live) return ESP_ERR_INVALID_STATE;
            host_i2c::live = false; return ESP_OK;
        }
        inline int i2c_master_write_to_device(int, uint8_t, const uint8_t* data, size_t length, unsigned) {
            if (!host_i2c::active || !host_i2c::live) return ESP_ERR_NOT_SUPPORTED;
            if (length < 2 || size_t(data[0]) + length - 1 > host_i2c::registers.size()) return ESP_ERR_INVALID_ARG;
            std::copy(data + 1, data + length, host_i2c::registers.begin() + data[0]);
            return ESP_OK;
        }
        inline int i2c_master_write_read_device(int, uint8_t, const uint8_t* data, size_t write_length,
                                              uint8_t* out, size_t read_length, unsigned) {
            if (!host_i2c::active || !host_i2c::live) return ESP_ERR_NOT_SUPPORTED;
            if (write_length != 1 || read_length != 1) return ESP_ERR_INVALID_ARG;
            *out = host_i2c::registers[data[0]]; return ESP_OK;
        }
    """,
    "driver/rmt_tx.h": """
        #include "gpio.h"
        #include <algorithm>
        #include <vector>
        using rmt_channel_handle_t = void*;
        struct rmt_encoder_t;
        using rmt_encoder_handle_t = rmt_encoder_t*;
        constexpr int RMT_CLK_SRC_DEFAULT = 0;
        struct rmt_symbol_word_t { unsigned level0, duration0, level1, duration1; };
        struct rmt_tx_channel_config_t {
            int clk_src; gpio_num_t gpio_num; size_t mem_block_symbols; unsigned resolution_hz;
            size_t trans_queue_depth; int intr_priority; struct { unsigned with_dma, invert_out; } flags;
        };
        struct rmt_transmit_config_t { int loop_count; struct { unsigned eot_level, queue_nonblocking; } flags; };
        struct rmt_tx_done_event_data_t { size_t num_symbols; };
        struct rmt_tx_event_callbacks_t { bool (*on_trans_done)(rmt_channel_handle_t, const rmt_tx_done_event_data_t*, void*); };
        namespace host_rmt {
            struct Channel {
                bool enabled = false;
                bool pin_reset = false;
                bool interrupt_freed = false;
                int gpio = -1;
                size_t depth = 0, pending = 0, symbols = 0;
                rmt_tx_event_callbacks_t callbacks{};
                void* user = nullptr;
            };
            inline bool active = false, stall = false, would_block = false;
            inline bool last_nonblocking = false;
            inline int last_wait_ms = 0;
            inline Channel* last_channel = nullptr;
            inline std::vector<Channel*> channels;
            inline int disable_fail_gpio = -1, delete_fail_gpio = -1, callbacks_fail_gpio = -1;
            inline unsigned disable_failures = 0, delete_failures = 0, callbacks_failures = 0;
            inline unsigned creates = 0, disables = 0, deletes = 0, live_encoders = 0;
            inline unsigned encoder_deletes = 0, submissions = 0;
            inline bool partial_delete_failure = false;
            inline unsigned unsafe_delete_retries = 0;
            inline Channel* channel_at(int gpio) {
                for (auto* channel : channels) if (channel->gpio == gpio) return channel;
                return nullptr;
            }
            inline void clear_faults() {
                disable_fail_gpio = delete_fail_gpio = callbacks_fail_gpio = -1;
                disable_failures = delete_failures = callbacks_failures = 0;
                stall = would_block = false;
                partial_delete_failure = false;
            }
        }
        inline int rmt_new_tx_channel(const rmt_tx_channel_config_t* cfg, rmt_channel_handle_t* out) {
            if (!host_rmt::active) return ESP_ERR_NOT_SUPPORTED;
            auto* c = new host_rmt::Channel;
            c->depth = cfg->trans_queue_depth;
            c->gpio = cfg->gpio_num;
            host_rmt::channels.push_back(c);
            ++host_rmt::creates;
            *out = host_rmt::last_channel = c;
            return ESP_OK;
        }
        inline int rmt_tx_register_event_callbacks(rmt_channel_handle_t handle, const rmt_tx_event_callbacks_t* cb, void* user) {
            auto* c = static_cast<host_rmt::Channel*>(handle);
            if (c->gpio == host_rmt::callbacks_fail_gpio && host_rmt::callbacks_failures) {
                --host_rmt::callbacks_failures; return ESP_FAIL;
            }
            c->callbacks = *cb; c->user = user; return ESP_OK;
        }
        inline int rmt_enable(rmt_channel_handle_t handle) {
            auto* c = static_cast<host_rmt::Channel*>(handle);
            if (c->enabled) return ESP_ERR_INVALID_STATE;
            c->enabled = true; return ESP_OK;
        }
        inline int rmt_disable(rmt_channel_handle_t handle) {
            auto* c = static_cast<host_rmt::Channel*>(handle);
            ++host_rmt::disables;
            if (c->gpio == host_rmt::disable_fail_gpio && host_rmt::disable_failures) {
                --host_rmt::disable_failures; return ESP_FAIL;
            }
            if (!c->enabled || c->pending) return ESP_ERR_INVALID_STATE;
            c->enabled = false; return ESP_OK;
        }
        inline int rmt_del_channel(rmt_channel_handle_t handle) {
            auto* c = static_cast<host_rmt::Channel*>(handle);
            ++host_rmt::deletes;
            if (c->interrupt_freed) {
                ++host_rmt::unsafe_delete_retries; return ESP_FAIL;
            }
            // IDF resets the pin before checking the channel's state/deleting it.
            c->pin_reset = true;
            if (c->enabled || c->pending) return ESP_ERR_INVALID_STATE;
            if (c->gpio == host_rmt::delete_fail_gpio && host_rmt::delete_failures) {
                // With PM enabled, IDF may free the interrupt and then fail to
                // delete its PM lock, retaining a stale internal interrupt ptr.
                c->interrupt_freed = host_rmt::partial_delete_failure;
                --host_rmt::delete_failures; return ESP_FAIL;
            }
            if (host_rmt::last_channel == c) host_rmt::last_channel = nullptr;
            host_rmt::channels.erase(std::find(host_rmt::channels.begin(), host_rmt::channels.end(), c));
            delete c; return ESP_OK;
        }
        inline int rmt_transmit(rmt_channel_handle_t handle, rmt_encoder_handle_t, const void*, size_t len, const rmt_transmit_config_t* cfg) {
            auto* c = static_cast<host_rmt::Channel*>(handle);
            if (!c->enabled || c->pin_reset) return ESP_ERR_INVALID_STATE;
            host_rmt::last_nonblocking = cfg->flags.queue_nonblocking;
            if (c->pending == c->depth) {
                // The pinned SDK would wait portMAX_DELAY here without this flag.
                host_rmt::would_block |= !host_rmt::last_nonblocking;
                return host_rmt::last_nonblocking ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
            }
            ++c->pending; ++host_rmt::submissions; c->symbols = len * 8 + 2; return ESP_OK;
        }
        inline int rmt_tx_wait_all_done(rmt_channel_handle_t handle, int timeout_ms) {
            auto* c = static_cast<host_rmt::Channel*>(handle);
            host_rmt::last_wait_ms = timeout_ms;
            if (host_rmt::stall && c->pending) return ESP_ERR_TIMEOUT;
            while (c->pending) {
                --c->pending;
                rmt_tx_done_event_data_t event{c->symbols};
                if (c->callbacks.on_trans_done) c->callbacks.on_trans_done(handle, &event, c->user);
            }
            return ESP_OK;
        }
    """,
    "driver/rmt_encoder.h": """
        #include "rmt_tx.h"
        enum rmt_encode_state_t { RMT_ENCODING_RESET = 0, RMT_ENCODING_COMPLETE = 1, RMT_ENCODING_MEM_FULL = 2 };
        struct rmt_encoder_t {
            size_t (*encode)(rmt_encoder_t*, rmt_channel_handle_t, const void*, size_t, rmt_encode_state_t*);
            int (*del)(rmt_encoder_t*); int (*reset)(rmt_encoder_t*);
        };
        struct rmt_bytes_encoder_config_t { rmt_symbol_word_t bit0, bit1; struct { unsigned msb_first; } flags; };
        struct rmt_copy_encoder_config_t {};
        inline int rmt_del_encoder(rmt_encoder_handle_t encoder) { ++host_rmt::encoder_deletes; return encoder->del(encoder); }
        inline int rmt_encoder_reset(rmt_encoder_handle_t encoder) { return encoder->reset(encoder); }
        inline int host_new_encoder(rmt_encoder_handle_t* out) {
            if (!host_rmt::active) return ESP_ERR_NOT_SUPPORTED;
            auto* e = new rmt_encoder_t{};
            ++host_rmt::live_encoders;
            e->del = [](rmt_encoder_t* p) { --host_rmt::live_encoders; delete p; return ESP_OK; };
            e->reset = [](rmt_encoder_t*) { return ESP_OK; };
            *out = e; return ESP_OK;
        }
        inline int rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t*, rmt_encoder_handle_t* out) { return host_new_encoder(out); }
        inline int rmt_new_copy_encoder(const rmt_copy_encoder_config_t*, rmt_encoder_handle_t* out) { return host_new_encoder(out); }
    """,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-sanitize", action="store_true", help="omit ASan/UBSan")
    parser.add_argument("--case", action="append", default=[], help="run a named test group")
    parser.add_argument("--baseline-ref", help="compare PCA implementation from an existing git ref")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    compiler = os.environ.get("CXX", "g++")
    if not shutil.which(compiler):
        parser.error(f"C++ compiler not found: {compiler}")

    with tempfile.TemporaryDirectory(prefix="lumigrid-output-") as temp_dir:
        temp = Path(temp_dir)
        for name, content in HOST_HEADERS.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            content = content.replace("GPIO_ENUM_VALUES", ", ".join(f"GPIO_NUM_{n} = {n}" for n in range(40)))
            path.write_text("#pragma once\n" + textwrap.dedent(content), encoding="utf-8")
        executable = temp / "output_drivers_test"
        baseline_pca = None
        if args.baseline_ref:
            baseline_pca = temp / "Pca9685Driver.cpp"
            baseline_pca.write_bytes(subprocess.run(
                ["git", "show", f"{args.baseline_ref}:LumigridLedNode/components/output/Pca9685Driver.cpp"],
                cwd=root.parent, check=True, capture_output=True).stdout)
        command = [compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-pthread"]
        if not args.no_sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        for directory in (temp, *(root / f"components/{name}/include" for name in ("output", "config", "board", "factory"))):
            command += ["-I", str(directory)]
        for source in ("test/output_drivers_test.cpp", "components/output/Pca9685Driver.cpp",
                       "components/output/ParallelRmtDriver.cpp", "components/output/AddressableDriver.cpp",
                       "components/output/OutputController.cpp", "components/board/BoardOutputSafety.cpp"):
            command.append(str(baseline_pca if baseline_pca and source == "components/output/Pca9685Driver.cpp"
                               else root / source))
        subprocess.run([*command, "-o", str(executable)], check=True)
        env = os.environ.copy()
        # LeakSanitizer requires /proc access unavailable in some CI sandboxes.
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
        env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
        result = subprocess.run([str(executable), *args.case], env=env, check=False)
        if result.returncode or args.case:
            return result.returncode
        pm_executable = temp / "output_drivers_pm_test"
        subprocess.run([*command, "-DCONFIG_PM_ENABLE=1", "-o", str(pm_executable)], check=True)
        return subprocess.run([str(pm_executable), "production_pm_delete_quarantine"], env=env, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
