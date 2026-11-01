#!/usr/bin/env python3
"""Exercise production CueRunner and current remote/local admission handlers.

The callback and HTTP handlers are extracted from LumigridRuntime.cpp and
FactoryApi.cpp at each build. ShowTransport and cJSON are also production code.
Persistence, rendering and the scheduler are recording host boundaries; this is
not a full runtime/API translation-unit build or a physical fade/sync test.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


CASES = [
    'pause_hold_uses_phase_anchor', 'pause_fade_keeps_remaining_time',
    'repeated_pause_keeps_phase_elapsed', 'pause_before_anchor_is_bounded',
    'missing_remote_list_preserves_local_playback',
    'invalid_remote_cue_preserves_active_content',
    'missing_remote_scene_preserves_local_playback',
    'invalid_remote_next_cue_is_rejected', 'invalid_remote_state_is_rejected',
    'missing_manual_next_scene_preserves_owner', 'missing_paused_next_scene_preserves_owner',
    'remote_stop_without_saved_content_is_accepted',
    'changed_remote_list_with_same_generation_is_applied',
    'same_slot_new_checksum_loads_new_content',
    'matching_active_content_survives_saved_edit',
    'mismatched_remote_content_keeps_previous_owner',
    'callback_missing_scene_does_not_preempt',
    'callback_missing_list_does_not_change_mode',
    'callback_mismatched_list_does_not_change_mode',
    'callback_invalid_cue_does_not_change_mode',
    'callback_matching_active_content_is_accepted',
    'callback_valid_scene_and_cue_are_accepted',
    'local_scene_missing_preserves_cue', 'local_activate_missing_preserves_cue',
    'local_timeline_missing_preserves_cue', 'local_timeline_corrupt_preserves_cue',
    'local_valid_scene_and_timeline_are_accepted',
    'local_cue_missing_preserves_timeline', 'local_cue_admitted_preempts_timeline',
]


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=root,
                        help='CueRunner/runtime source tree, for baseline reproduction')
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--no-sanitize', action='store_true')
    args = parser.parse_args()
    source = args.source.resolve()
    suite = root / 'tests/cue_runner_host'
    compiler = os.environ.get('CXX', 'g++')
    if not shutil.which(compiler):
        parser.error(f'C++ compiler not found: {compiler}')
    runtime = (source / 'main/LumigridRuntime.cpp').read_text()
    begin = runtime.index('  node_sync_.set_remote_transport_callback(')
    end = runtime.index('  node_sync_.set_transport_callback(', begin)
    callback = runtime[begin:end].strip()
    test_source = (suite / 'cue_runner_tests.cpp.in').read_text().replace('@@REMOTE_CALLBACK@@', callback)
    factory = (source / 'components/factory/FactoryApi.cpp').read_text()
    handlers = []
    for start, end in [('esp_err_t post_show_control(', 'esp_err_t get_show_status('),
                       ('esp_err_t post_activate(', 'esp_err_t get_device_config(')]:
        begin = factory.index(start)
        handlers.append(factory[begin:factory.index(end, begin)])
    test_source = test_source.replace('@@FACTORY_HANDLERS@@', '\n'.join(handlers))
    cjson = root / 'managed_components/espressif__cjson/cJSON'
    with tempfile.TemporaryDirectory(prefix='lg-cue-runner-') as temp:
        temp = Path(temp)
        headers = {
            'esp_err.h': '''#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=0x102,
 ESP_ERR_INVALID_STATE=0x103, ESP_ERR_NOT_FOUND=0x105, ESP_ERR_INVALID_CRC=0x109;
inline const char* esp_err_to_name(esp_err_t) { return "host-error"; }
''',
            'esp_log.h': '#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n',
            'esp_random.h': '#pragma once\n#include <cstdint>\ninline uint32_t esp_random() { return 42; }\n',
            'driver/gpio.h': '#pragma once\nenum gpio_num_t { ' +
                ', '.join(f'GPIO_NUM_{pin} = {pin}' for pin in range(40)) + ' };\n',
            'freertos/FreeRTOS.h': '#pragma once\nusing portMUX_TYPE = int;\n#define portMUX_INITIALIZER_UNLOCKED 0\n',
            'freertos/task.h': '#pragma once\nusing TaskHandle_t = void*;\n',
            'freertos/semphr.h': '#pragma once\nusing SemaphoreHandle_t = void*;\n',
            'lwip/sockets.h': '#pragma once\n#include <sys/socket.h>\n#include <netinet/in.h>\n',
        }
        for name, text in headers.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        (temp / 'tests.cpp').write_text(test_source)
        executable = temp / 'cue-tests'
        command = [compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra',
                   '-Wno-unused-variable', '-pthread']
        if not args.no_sanitize:
            command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        for include in (temp, suite / 'include', source / 'components/show/include', cjson,
                        root / 'components/config/include', root / 'components/board/include',
                        root / 'components/sync/include', root / 'components/signage/include'):
            command += ['-I', str(include)]
        cflags = ['-O1', '-g']
        if not args.no_sanitize:
            cflags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie']
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', *cflags,
                        '-c', str(cjson / 'cJSON.c'), '-o', str(temp / 'cjson.o')], check=True)
        command += [str(source / 'components/show/CueRunner.cpp'),
                    str(source / 'components/show/ShowTransport.cpp'), str(temp / 'tests.cpp'),
                    str(temp / 'cjson.o'),
                    '-o', str(executable)]
        subprocess.run(command, check=True)
        env = os.environ.copy()
        env.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1')
        env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
        cases = args.case or CASES
        failed = 0
        for case in cases:
            result = subprocess.run([str(executable), case], capture_output=True, text=True,
                                    env=env, timeout=5)
            print(result.stdout.strip() or f'FAIL {case}')
            if result.stderr:
                print(result.stderr.strip())
            failed += result.returncode != 0
        print(f'CueRunner/callback production-source scenarios: {len(cases)-failed}/{len(cases)} passed')
        return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
