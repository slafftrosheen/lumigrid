#!/usr/bin/env python3
"""Verify terminal job ownership using the production JobManager implementation."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "JobManager.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using namespace lg;
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string name = argv[1];
    JobManager jobs;
    OperationJob result;
    uint32_t id = 0;
    if (name == "zero_is_not_a_job") {
        assert(!jobs.get_job(0, result));
        jobs.update_progress(0, JobState::Running, 20);
        assert(!jobs.get_current(result));
    } else {
        assert(jobs.start_job(JobType::BurnIn, &id) == ESP_OK && id != 0);
        jobs.update_progress(id, JobState::Running, 40);
        if (name == "active_job_excludes_second_worker") {
            uint32_t next = 0;
            assert(jobs.start_job(JobType::Ota, &next) == ESP_ERR_INVALID_STATE && next == 0);
        } else {
            const auto terminal = name == "failed_stays_failed" ? JobState::Failed :
                                  name == "completed_stays_completed" ? JobState::Completed : JobState::Cancelled;
            if (terminal == JobState::Failed) jobs.fail_job(id, ESP_FAIL, "physical commit failed");
            else if (terminal == JobState::Completed) jobs.complete_job(id);
            else jobs.cancel_job(id);
            assert(jobs.get_job(id, result));
            const OperationJob settled = result;
            // Simulate an already-entered worker publishing after cancellation
            // or a terminal result. Completion/cancellation order must not erase
            // the first settled result.
            jobs.update_progress(id, JobState::Running, 90);
            jobs.complete_job(id);
            jobs.fail_job(id, -77, "late error");
            jobs.cancel_job(id);
            assert(jobs.get_job(id, result));
            assert(result.state == terminal && result.progress == settled.progress);
            assert(result.error_code == settled.error_code);
            assert(std::strcmp(result.error_detail, settled.error_detail) == 0);
            uint32_t next = 0;
            assert(jobs.start_job(JobType::Restore, &next) == ESP_OK && next != id);
            jobs.complete_job(id); // A retired worker cannot complete the next job.
            assert(jobs.get_current(result) && result.job_id == next && result.state == JobState::Queued);
            assert(jobs.get_job(id, result) && result.state == terminal);
        }
    }
    std::cout << "PASS " << name << '\n';
}
'''


def main():
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        raise SystemExit("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-control-jobs-") as directory:
        build = Path(directory)
        (build / "esp_err.h").write_text(
            "#pragma once\nusing esp_err_t = int;\n"
            "constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = 0x103;\n")
        (build / "harness.cpp").write_text(HARNESS)
        executable = build / "jobs"
        subprocess.run([compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-pthread",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                        "-I", str(build), "-I", str(ROOT / "components/control/include"),
                        str(ROOT / "components/control/JobManager.cpp"), str(build / "harness.cpp"),
                        "-o", str(executable)], check=True)
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
               "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
        cases = ("zero_is_not_a_job", "active_job_excludes_second_worker", "cancelled_stays_cancelled",
                 "failed_stays_failed", "completed_stays_completed")
        failures = []
        for case in cases:
            result = subprocess.run([str(executable), case], env=env, capture_output=True, text=True, timeout=10)
            print(result.stdout or result.stderr, end="")
            if result.returncode:
                failures.append(case)
        if failures:
            raise SystemExit(f"{len(failures)}/{len(cases)} job cases failed: {', '.join(failures)}")
        print(f"PASS: {len(cases)} production job state scenarios")


if __name__ == "__main__":
    main()
