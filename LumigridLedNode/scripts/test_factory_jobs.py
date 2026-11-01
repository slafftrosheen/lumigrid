#!/usr/bin/env python3
"""Run production FactoryRunner/JobManager cancellation with recording outputs.

Only the SDK/RTOS boundaries are simulated. Tests deliberately leave queued
commands unprocessed until the worker's production apply_command is invoked.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
from test_lighting_state import HEADERS as LIGHTING_HEADERS

ROOT = Path(__file__).resolve().parents[1]
HEADERS = dict(LIGHTING_HEADERS)
HEADERS["freertos/FreeRTOS.h"] += "\nusing UBaseType_t = unsigned;\nusing TaskHandle_t = void*;\n"
HEADERS["freertos/task.h"] += "\ninline void vTaskDelayUntil(TickType_t* last, TickType_t period) { *last += period; }\n"
HEADERS["freertos/queue.h"] = r'''
    #include "FreeRTOS.h"
    #include <deque>
    #include <vector>
    #include <mutex>
    #include <cstring>
    struct HostQueue {
        std::mutex mutex;
        size_t capacity, item_size;
        bool deleted = false;
        std::deque<std::vector<unsigned char>> items;
    };
    using QueueHandle_t = HostQueue*;
    inline QueueHandle_t xQueueCreate(unsigned capacity, size_t item_size) {
        auto* queue = new HostQueue; queue->capacity = capacity; queue->item_size = item_size; return queue;
    }
    inline int xQueueSend(QueueHandle_t queue, const void* value, TickType_t) {
        if (!queue) return pdFALSE;
        std::lock_guard<std::mutex> lock(queue->mutex);
        if (queue->deleted || queue->items.size() == queue->capacity) return pdFALSE;
        const auto* bytes = static_cast<const unsigned char*>(value);
        queue->items.emplace_back(bytes, bytes + queue->item_size); return pdTRUE;
    }
    inline int xQueueReceive(QueueHandle_t queue, void* value, TickType_t) {
        if (!queue) return pdFALSE;
        std::lock_guard<std::mutex> lock(queue->mutex);
        if (queue->deleted || queue->items.empty()) return pdFALSE;
        std::memcpy(value, queue->items.front().data(), queue->item_size);
        queue->items.pop_front(); return pdTRUE;
    }
    inline int xQueueReset(QueueHandle_t queue) {
        std::lock_guard<std::mutex> lock(queue->mutex); queue->items.clear(); return pdTRUE;
    }
    inline void vQueueDelete(QueueHandle_t queue) { if (queue) queue->deleted = true; }
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--runner-source", type=Path,
                        default=ROOT / "components/factory/FactoryRunner.cpp",
                        help="Alternative FactoryRunner source for regression comparison")
    args = parser.parse_args()
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        parser.error("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-factory-jobs-") as directory:
        build = Path(directory)
        for name, content in HEADERS.items():
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            content = content.replace("GPIO_ENUM_VALUES", ", ".join(f"GPIO_NUM_{n} = {n}" for n in range(40)))
            path.write_text("#pragma once\n" + textwrap.dedent(content))
        executable = build / "factory_jobs"
        command = [compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-include", "esp_err.h",
                   "-pthread", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        for directory in (build, *(ROOT / f"components/{name}/include" for name in
                                  ("output", "config", "board", "factory", "signage", "control", "certification"))):
            command += ["-I", str(directory)]
        sources = ["test/factory_jobs_test.cpp", "components/board/BoardOutputSafety.cpp"]
        sources += [f"components/output/{name}.cpp" for name in
                    ("Pca9685Driver", "ParallelRmtDriver", "AddressableDriver", "OutputController")]
        sources += ["components/signage/ControlSourceManager.cpp",
                    "components/factory/FactoryEffects.cpp", "components/control/JobManager.cpp", "components/control/CommandJournal.cpp"]
        command += [str(ROOT / source) for source in sources]
        command += [str(args.runner_source)]
        subprocess.run([*command, "-o", str(executable)], check=True)
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
               "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
        cases = args.case or ["cancel_waits_for_blackout", "cancel_queue_failure_retains_owner", "start_queue_failure_finishes_job",
                              "stop_repeated_restart", "cancel_blackout_failure_is_failed", "abort_before_pending_job_keeps_job",
                              "stale_abort_does_not_cancel_new_session", "stop_waiting_api_callers",
                              "confirm_waits_for_blackout", "confirm_stops_all_producers",
                              "confirm_blackout_failure_is_failed", "stale_confirmation_preserves_active_session",
                              "duplicate_confirmation_rejected", "empty_confirmation_rejected"]
        failures = []
        for case in cases:
            result = subprocess.run([str(executable), case], env=env, capture_output=True, text=True, timeout=10)
            print(result.stdout or result.stderr, end="")
            if result.returncode:
                failures.append(case)
        if failures:
            raise SystemExit(f"{len(failures)}/{len(cases)} factory job cases failed: {', '.join(failures)}")
        print(f"PASS: {len(cases)} production factory job scenarios")


if __name__ == "__main__":
    main()
