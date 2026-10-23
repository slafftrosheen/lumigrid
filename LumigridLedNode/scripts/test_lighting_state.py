#!/usr/bin/env python3
"""Exercise production lighting/control/output with deterministic RTOS boundaries.

The RTOS fake records semaphore deletion with admitted waiters and lets tests
pause a producer at lock acquisition. Rendering, arbitration, power limiting,
controller commits and recording hardware transports execute production code.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
from test_output_drivers import HOST_HEADERS

ROOT = Path(__file__).resolve().parents[1]
HEADERS = dict(HOST_HEADERS)
HEADERS["esp_timer.h"] = r'''
    #include <atomic>
    #include <cstdint>
    inline std::atomic<int64_t> host_now_us{1000000};
    inline int64_t esp_timer_get_time() { return host_now_us.load(); }
'''
HEADERS["esp_system.h"] += "\nusing esp_reset_reason_t = int;\n"
HEADERS["freertos/FreeRTOS.h"] += "\nconstexpr int pdPASS = 1;\n"
HEADERS["freertos/semphr.h"] = r'''
    #include "FreeRTOS.h"
    #include <atomic>
    #include <chrono>
    #include <condition_variable>
    #include <functional>
    #include <mutex>
    struct HostSemaphore {
        std::recursive_timed_mutex mutex;
        std::mutex binary_mutex;
        std::condition_variable binary_ready;
        bool binary = false, available = false;
        std::atomic<bool> deleted{false};
        std::atomic<unsigned> waiters{0};
    };
    using SemaphoreHandle_t = HostSemaphore*;
    inline SemaphoreHandle_t host_last_binary = nullptr;
    inline std::atomic<unsigned> host_unsafe_deletes{0};
    inline thread_local std::function<void(SemaphoreHandle_t)> host_before_take;
    inline thread_local std::function<void(SemaphoreHandle_t)> host_after_give;
    inline thread_local SemaphoreHandle_t host_after_give_target = nullptr;
    inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new HostSemaphore; }
    inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return xSemaphoreCreateMutex(); }
    inline SemaphoreHandle_t xSemaphoreCreateBinary() {
        auto* sem = new HostSemaphore; sem->binary = true; host_last_binary = sem; return sem;
    }
    inline int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout) {
        if (!sem || sem->deleted) return pdFALSE;
        ++sem->waiters;
        if (host_before_take) { auto hook = std::move(host_before_take); host_before_take = {}; hook(sem); }
        if (sem->deleted) { --sem->waiters; return pdFALSE; }
        bool taken = false;
        if (sem->binary) {
            std::unique_lock<std::mutex> lock(sem->binary_mutex);
            if (timeout == portMAX_DELAY) {
                sem->binary_ready.wait(lock, [&] { return sem->available; }); taken = true;
            } else {
                taken = sem->binary_ready.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return sem->available; });
            }
            if (taken) sem->available = false;
        } else if (timeout == portMAX_DELAY) { sem->mutex.lock(); taken = true; }
        else { taken = sem->mutex.try_lock_for(std::chrono::milliseconds(timeout)); }
        --sem->waiters;
        return taken ? pdTRUE : pdFALSE;
    }
    inline int xSemaphoreGive(SemaphoreHandle_t sem) {
        if (!sem || sem->deleted) return pdFALSE;
        if (sem->binary) {
            std::lock_guard<std::mutex> lock(sem->binary_mutex);
            sem->available = true; sem->binary_ready.notify_all();
        } else sem->mutex.unlock();
        if (host_after_give && (!host_after_give_target || host_after_give_target == sem)) {
            auto hook = std::move(host_after_give); host_after_give = {}; host_after_give_target = nullptr; hook(sem);
        }
        return pdTRUE;
    }
    inline int xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t timeout) { return xSemaphoreTake(sem, timeout); }
    inline int xSemaphoreGiveRecursive(SemaphoreHandle_t sem) { return xSemaphoreGive(sem); }
    inline void vSemaphoreDelete(SemaphoreHandle_t sem) {
        if (sem && sem->waiters) ++host_unsafe_deletes;
        if (sem) sem->deleted = true;
        // Keep a tombstone so a queued waiter can report the invalid deletion
        // without relying on undefined behavior in the host mutex implementation.
    }
'''
HEADERS["freertos/task.h"] = r'''
    #include "FreeRTOS.h"
    #include "semphr.h"
    #include "esp_timer.h"
    using TaskHandle_t = void*;
    inline bool host_task_ack = true;
    inline uintptr_t host_task_sequence = 16;
    inline thread_local TaskHandle_t host_task_identity = nullptr;
    inline int xPortGetCoreID() { return 1; }
    inline TaskHandle_t xTaskGetCurrentTaskHandle() {
        static thread_local char identity;
        return host_task_identity ? host_task_identity : &identity;
    }
    inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned, void*, int, TaskHandle_t* out, int) {
        *out = reinterpret_cast<void*>(++host_task_sequence); return pdPASS;
    }
    inline int xTaskAbortDelay(TaskHandle_t) { if (host_task_ack) xSemaphoreGive(host_last_binary); return pdTRUE; }
    inline void vTaskDelete(TaskHandle_t) {}
    inline void vTaskDelay(TickType_t) {}
    inline TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(host_now_us.load() / 1000); }
    inline TaskHandle_t xTaskGetHandle(const char*) { return nullptr; }
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--baseline-ref", help="Compare lighting/scene/controller source from an existing git ref")
    args = parser.parse_args()
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        parser.error("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-lighting-state-") as directory:
        build = Path(directory)
        for name, content in HEADERS.items():
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            content = content.replace("GPIO_ENUM_VALUES", ", ".join(f"GPIO_NUM_{n} = {n}" for n in range(40)))
            path.write_text("#pragma once\n" + textwrap.dedent(content))
        executable = build / "lighting_state"
        baseline_sources = {}
        if args.baseline_ref:
            for relative in ("components/signage/LightingRuntime.cpp", "components/signage/include/LightingRuntime.hpp",
                             "components/signage/SceneManager.cpp", "components/signage/include/SceneManager.hpp",
                             "components/output/OutputController.cpp", "components/output/include/OutputController.hpp"):
                path = build / Path(relative).name
                source = subprocess.run(["git", "show", f"{args.baseline_ref}:LumigridLedNode/{relative}"],
                                        cwd=ROOT.parent, check=True, capture_output=True)
                path.write_bytes(source.stdout)
                baseline_sources[relative] = path
        command = [compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-include", "esp_err.h",
                   "-pthread", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        for directory in (build, *(ROOT / f"components/{name}/include" for name in
                                  ("output", "config", "board", "factory", "signage", "control"))):
            command += ["-I", str(directory)]
        sources = ["test/lighting_state_test.cpp", "components/board/BoardOutputSafety.cpp"]
        sources += [f"components/output/{name}.cpp" for name in
                    ("Pca9685Driver", "ParallelRmtDriver", "AddressableDriver", "OutputController")]
        sources += [f"components/signage/{name}.cpp" for name in
                    ("LightingRuntime", "ControlSourceManager", "SceneManager", "PowerBudget", "ColorPipeline",
                     "SceneTransition", "EffectRenderer", "Compositor", "OutputScheduler", "AutomationEngine", "GammaLut")]
        command += [str(baseline_sources.get(source, ROOT / source)) for source in sources]
        subprocess.run([*command, "-o", str(executable)], check=True)
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
               "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
        cases = args.case or ["stop_repeated_restart", "stop_waiting_producer", "stop_timeout_retains_task",
                              "stop_restart_waiting_producer",
                              "live_short_frame_tail", "manual_frame_snapshot", "new_estop_during_reset_blackout",
                              "new_estop_before_hardware_clear", "normal_estop_clear_requires_two_frames",
                              "armed_requires_a_commit", "reload_callback_can_clear_live",
                              "restart_snapshot_excludes_initialization", "restart_discards_frozen_transition"]
        failures = []
        for case in cases:
            result = subprocess.run([str(executable), case], env=env, capture_output=True, text=True, timeout=10)
            print(result.stdout or result.stderr, end="")
            if result.returncode:
                failures.append(case)
        if failures:
            raise SystemExit(f"{len(failures)}/{len(cases)} lighting cases failed: {', '.join(failures)}")
        print(f"PASS: {len(cases)} production lighting state scenarios")


if __name__ == "__main__":
    main()
