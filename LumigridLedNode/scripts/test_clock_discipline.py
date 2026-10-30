#!/usr/bin/env python3
"""Compile the real clock implementation against host timer/critical-section adapters.
No ESP32, radio, output timing or hardware acceptance is implied by this suite.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = ['initial', 'zero_time_sample', 'stale_lock', 'domain_change',
         'invalid_epoch', 'invalid_heartbeat', 'difference_overflow',
         'read_overflow', 'age_saturation', 'bounded_slew',
         'large_correction', 'reacquisition', 'epoch_holdover']
HARNESS = r'''
#include "ClockDiscipline.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
static int64_t timer_us = 0;
extern "C" int64_t esp_timer_get_time() { return timer_us; }
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string name = argv[1];
    const uint64_t max_ms = static_cast<uint64_t>(INT64_MAX) / 1000ULL;
    lg::ClockDiscipline clock;
    try {
        if (name == "initial") {
            require(!clock.status().locked && !clock.has_epoch(), "uninitialized clock must not be locked");
            require(clock.status().last_update_age_ms == UINT32_MAX, "initial age must be unknown");
        } else if (name == "zero_time_sample") {
            clock.discipline(500, false);
            require(clock.status().last_update_age_ms == 0, "tick-zero sample must not be confused with no sample");
        } else if (name == "stale_lock") {
            timer_us = 1000; clock.discipline(1000, false);
            timer_us += 3000001;
            require(!clock.status().locked, "clock lock must expire after missing heartbeats");
        } else if (name == "domain_change") {
            timer_us = 1000000; clock.set_epoch(1700000000000ULL);
            clock.discipline(2000, false);
            require(!clock.has_epoch() && !clock.status().epoch_valid, "leader uptime must not retain epoch-valid flag");
            require(clock.now_ms() == 2000, "domain switch must immediately rebase");
        } else if (name == "invalid_epoch") {
            clock.set_epoch(UINT64_MAX);
            require(!clock.has_epoch() && !clock.status().locked, "unrepresentable epoch must not initialize clock");
        } else if (name == "invalid_heartbeat") {
            timer_us = 1000; clock.discipline(1000, true);
            timer_us += 100000; const auto before = clock.now_ms();
            clock.discipline(UINT64_MAX, false);
            require(clock.now_ms() == before && clock.has_epoch(), "invalid heartbeat must preserve valid clock");
            require(clock.status().last_update_age_ms == 100, "invalid heartbeat must not refresh sample age");
        } else if (name == "difference_overflow") {
            clock.set_epoch(max_ms);
            timer_us = INT64_MAX - 1;
            clock.discipline(0, false);
            require(clock.now_ms() == 0, "extreme correction must be defined and rebase to zero");
        } else if (name == "read_overflow") {
            clock.set_epoch(max_ms); timer_us = INT64_MAX - 1;
            require(clock.now_ms() == max_ms, "clock read must saturate instead of signed overflow");
        } else if (name == "age_saturation") {
            timer_us = 1000; clock.discipline(1000, false);
            timer_us += (static_cast<int64_t>(UINT32_MAX) + 100) * 1000;
            require(clock.status().last_update_age_ms == UINT32_MAX, "age must saturate, not wrap into fresh status");
        } else if (name == "bounded_slew") {
            timer_us = 1000; clock.discipline(1000, false);
            timer_us += 100000; clock.discipline(1110, false);
            require(clock.now_ms() == 1102, "small positive correction must retain 2 ms slew bound");
            timer_us += 100000; clock.discipline(1190, false);
            require(clock.now_ms() == 1200, "small negative correction must retain 2 ms slew bound");
        } else if (name == "large_correction") {
            timer_us = 1000; clock.discipline(1000, false);
            clock.discipline(10000, false);
            require(clock.now_ms() == 10000, "large correction must step to leader");
        } else if (name == "reacquisition") {
            timer_us = 1000; clock.discipline(1000, false);
            timer_us += 4000000; clock.discipline(5010, false);
            require(clock.now_ms() == 5010, "stale clock must reacquire instead of slowly slewing stale offset");
        } else if (name == "epoch_holdover") {
            timer_us = 1000; clock.set_epoch(1700000000000ULL);
            timer_us += 4000000;
            require(clock.has_epoch() && !clock.status().locked, "holdover epoch remains usable but must not report fresh lock");
            require(clock.now_ms() == 1700000004000ULL, "holdover clock must keep advancing");
        } else return 2;
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << name << ": " << e.what() << '\n'; return 1;
    }
}
'''

def main() -> int:
    with tempfile.TemporaryDirectory(prefix='lg-clock-') as folder:
        tmp = Path(folder)
        (tmp / 'freertos').mkdir()
        (tmp / 'freertos/FreeRTOS.h').write_text(
            '#pragma once\n#include <mutex>\nusing portMUX_TYPE = std::mutex;\n'
            '#define portMUX_INITIALIZER_UNLOCKED {}\n'
            '#define portENTER_CRITICAL(m) (m)->lock()\n'
            '#define portEXIT_CRITICAL(m) (m)->unlock()\n')
        (tmp / 'esp_timer.h').write_text('#pragma once\n#include <cstdint>\nextern "C" int64_t esp_timer_get_time();\n')
        (tmp / 'test.cpp').write_text(HARNESS)
        output = tmp / 'clock_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                   '-fno-sanitize-recover=all', '-pthread', '-I', str(tmp),
                   '-I', str(ROOT / 'components/sync/include'),
                   str(ROOT / 'components/sync/ClockDiscipline.cpp'),
                   str(tmp / 'test.cpp'), '-o', str(output)]
        subprocess.run(command, check=True)
        failures = 0
        for case in CASES:
            result = subprocess.run([str(output), case], text=True, capture_output=True)
            print(result.stdout + result.stderr, end='')
            failures += result.returncode != 0
        print(f'Clock production-source tests: {len(CASES) - failures}/{len(CASES)} passed')
        return 1 if failures else 0

if __name__ == '__main__':
    sys.exit(main())
