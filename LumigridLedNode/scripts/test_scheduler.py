#!/usr/bin/env python3
"""Exercise production Timeline admission, playback math, and API callbacks.

Compiles full Scheduler, TimelineEvaluator, CueRunner, ShowTransport and
ClockDiscipline sources. The current runtime callback and show handlers are
extracted verbatim at build time. Storage, rendering and SDK task primitives
are recording host boundaries; this is not physical/FreeRTOS qualification.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    suite = root / 'tests/scheduler_host'
    fixture = (suite / 'scheduler_tests.cpp.in').read_text()
    cases = re.findall(r'name == "([a-z_]+)"', fixture)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=root,
                        help='Alternative source tree for baseline reproduction')
    parser.add_argument('--case', action='append', choices=cases)
    parser.add_argument('--no-sanitize', action='store_true')
    args = parser.parse_args()
    source = args.source.resolve()
    compiler = os.environ.get('CXX', 'g++')
    if not shutil.which(compiler):
        parser.error(f'C++ compiler not found: {compiler}')

    runtime = (source / 'main/LumigridRuntime.cpp').read_text()
    begin = runtime.index('  node_sync_.set_remote_transport_callback(')
    end = runtime.index('  node_sync_.set_transport_callback(', begin)
    fixture = fixture.replace('@@REMOTE_CALLBACK@@', runtime[begin:end].strip())
    factory = (source / 'components/factory/FactoryApi.cpp').read_text()
    handlers = []
    for start, end in [('esp_err_t post_show_control(', 'esp_err_t get_show_status('),
                       ('esp_err_t post_activate(', 'esp_err_t get_device_config(')]:
        begin = factory.index(start)
        handlers.append(factory[begin:factory.index(end, begin)])
    fixture = fixture.replace('@@FACTORY_HANDLERS@@', '\n'.join(handlers))
    cjson = root / 'managed_components/espressif__cjson/cJSON'
    with tempfile.TemporaryDirectory(prefix='lg-scheduler-') as temporary:
        temp = Path(temporary)
        headers = {
            'esp_err.h': '''#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_ARG=0x102,
 ESP_ERR_INVALID_STATE=0x103, ESP_ERR_NOT_FOUND=0x105, ESP_ERR_INVALID_CRC=0x109;
inline const char* esp_err_to_name(esp_err_t) { return "host-error"; }
''',
            'esp_log.h': '#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n',
            'esp_random.h': '#pragma once\n#include <cstdint>\ninline uint32_t esp_random() { return 42; }\n',
            'esp_timer.h': '#pragma once\n#include "host.hpp"\ninline int64_t esp_timer_get_time() { return host::timer_us.load(); }\n',
            'driver/gpio.h': '#pragma once\nenum gpio_num_t { ' +
                ', '.join(f'GPIO_NUM_{pin} = {pin}' for pin in range(40)) + ' };\n',
            'freertos/FreeRTOS.h': '''#pragma once
#include "host.hpp"
#include <cstdlib>
#include <cstring>
using BaseType_t = int;
using TickType_t = uint32_t;
constexpr BaseType_t pdTRUE = 1, pdPASS = 1;
struct portMUX_TYPE { std::mutex mutex; };
#define portMUX_INITIALIZER_UNLOCKED {}
#define pdMS_TO_TICKS(value) (value)
inline void portENTER_CRITICAL(portMUX_TYPE* mux) {
    mux->mutex.lock(); ++host::critical_depth;
}
inline void portEXIT_CRITICAL(portMUX_TYPE* mux) {
    --host::critical_depth; mux->mutex.unlock();
}
''',
            'freertos/task.h': '''#pragma once
#include "freertos/FreeRTOS.h"
using TaskHandle_t = void*;
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t,
    void*, unsigned, TaskHandle_t* task, int) { *task = reinterpret_cast<void*>(1); return pdPASS; }
inline void xTaskAbortDelay(TaskHandle_t) {}
inline void vTaskDelay(TickType_t) {}
inline void vTaskDelayUntil(TickType_t*, TickType_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline TickType_t xTaskGetTickCount() { return 0; }
''',
            'freertos/semphr.h': '''#pragma once
#include "freertos/FreeRTOS.h"
using SemaphoreHandle_t = void*;
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return reinterpret_cast<void*>(1); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
''',
            'esp_sntp.h': '''#pragma once
constexpr int ESP_SNTP_OPMODE_POLL = 0, SNTP_SYNC_STATUS_COMPLETED = 1;
inline void esp_sntp_setoperatingmode(int) {}
inline void esp_sntp_setservername(int, const char*) {}
inline void esp_sntp_init() {}
inline void esp_sntp_stop() {}
inline int esp_sntp_get_sync_status() { return 0; }
''',
            'lwip/sockets.h': '#pragma once\n#include <sys/socket.h>\n#include <netinet/in.h>\n',
        }
        for name, text in headers.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        (temp / 'tests.cpp').write_text(fixture)
        executable = temp / 'scheduler-tests'
        command = [compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra',
                   '-Wno-unused-variable', '-pthread']
        sanitizers = [] if args.no_sanitize else [
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer', '-fno-pie']
        command += sanitizers
        if not args.no_sanitize:
            command += ['-no-pie']
        for include in (temp, suite / 'include', source / 'components/scheduler/include',
                        source / 'components/show/include', cjson,
                        root / 'components/config/include', root / 'components/board/include',
                        root / 'components/sync/include', root / 'components/signage/include'):
            command += ['-I', str(include)]
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-g', *sanitizers,
                        '-c', str(cjson / 'cJSON.c'), '-o', str(temp / 'cjson.o')], check=True)
        command += [str(source / path) for path in (
            'components/scheduler/Scheduler.cpp', 'components/scheduler/TimelineEvaluator.cpp',
            'components/show/CueRunner.cpp', 'components/show/ShowTransport.cpp',
            'components/sync/ClockDiscipline.cpp')]
        command += [str(temp / 'tests.cpp'), str(temp / 'cjson.o'), '-o', str(executable)]
        subprocess.run(command, check=True)
        env = os.environ.copy()
        env.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1')
        env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
        selected = args.case or cases
        failed = 0
        for case in selected:
            result = subprocess.run([str(executable), case], capture_output=True, text=True,
                                    env=env, timeout=5)
            print(result.stdout.strip() or f'FAIL {case}')
            if result.stderr:
                print(result.stderr.strip())
            failed += result.returncode != 0
        print(f'Scheduler/callback production-source scenarios: {len(selected)-failed}/{len(selected)} passed')
        return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
