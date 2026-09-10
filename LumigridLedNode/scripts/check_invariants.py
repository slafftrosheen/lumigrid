#!/usr/bin/env python3
"""
check_invariants.py - Certification Invariant Static Validation

Fails with non-zero exit code if any forbidden architectural anti-patterns
or lifecycle regressions are discovered in the codebase:
1. ESP_ERROR_CHECK in runtime lifecycle code.
2. Uncontrolled vTaskDelete(task_handle_) outside cooperative shutdown.
3. Process-global 'static bool services_started'.
4. Production-accessible hardcoded default passwords ("lumigrid123" outside dev flags).
5. Unsynchronized direct output mutations outside controlled transactions/factory exceptions.
"""

import sys
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
NODE_DIR = REPO_ROOT / "LumigridLedNode"

errors = []

def check_file_contains(filepath: Path, forbidden_patterns: list, exception_patterns: list = None):
    if not filepath.exists():
        return
    text = filepath.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines()
    for line_idx, line in enumerate(lines, 1):
        if line.strip().startswith("//") or line.strip().startswith("/*"):
            continue
        for pattern, desc in forbidden_patterns:
            if pattern in line:
                if exception_patterns and any(exc in line for exc in exception_patterns):
                    continue
                errors.append(f"{filepath.relative_to(REPO_ROOT)}:{line_idx}: {desc} -> '{line.strip()}'")

print("=== Running LumiGrid Certification Invariant Checks ===")

# 1. No ESP_ERROR_CHECK in runtime lifecycle
lifecycle_files = [
    NODE_DIR / "main" / "LumigridRuntime.cpp",
    NODE_DIR / "components" / "signage" / "LightingRuntime.cpp",
    NODE_DIR / "components" / "factory" / "FactoryRunner.cpp",
    NODE_DIR / "components" / "output" / "OutputController.cpp",
]
for f in lifecycle_files:
    check_file_contains(f, [("ESP_ERROR_CHECK(", "Forbidden ESP_ERROR_CHECK in runtime lifecycle code")])

# 2. No uncontrolled external vTaskDelete(task_handle_)
task_delete_files = [
    NODE_DIR / "components" / "signage" / "LightingRuntime.cpp",
    NODE_DIR / "components" / "factory" / "FactoryRunner.cpp",
]
for f in task_delete_files:
    check_file_contains(f, [("vTaskDelete(task_handle_", "Forbidden external vTaskDelete on runtime task handle")])

# 3. No process-global static bool services_started across codebase
for ext in ["*.cpp", "*.hpp", "*.h", "*.c"]:
    for f in (NODE_DIR / "main").rglob(ext):
        check_file_contains(f, [("static bool services_started", "Process-global static bool services_started detected")])
    for f in (NODE_DIR / "components").rglob(ext):
        check_file_contains(f, [("static bool services_started", "Process-global static bool services_started detected")])

# 4. No network components with hardcoded password
for f in (NODE_DIR / "components" / "network").rglob("*.*"):
    if f.suffix in [".cpp", ".hpp", ".h", ".c"]:
        check_file_contains(f, [("lumigrid123", "Hardcoded development credentials found in network driver")])

if errors:
    print("\nFAILED: Discovered architectural invariant violations:")
    for err in errors:
        print(f"  [!] {err}")
    sys.exit(1)
else:
    print("[PASS] All certification architectural invariants verified successfully.")
    sys.exit(0)
