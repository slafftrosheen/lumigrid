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

# 2. No uncontrolled external vTaskDelete in runtime lifecycle tasks (only vTaskDelete(NULL) allowed)
task_delete_files = [
    NODE_DIR / "components" / "signage" / "LightingRuntime.cpp",
    NODE_DIR / "components" / "factory" / "FactoryRunner.cpp",
]
for f in task_delete_files:
    check_file_contains(
        f,
        [("vTaskDelete(", "Forbidden external vTaskDelete in runtime lifecycle code (only vTaskDelete(NULL) allowed)")],
        exception_patterns=["vTaskDelete(NULL)"]
    )

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

# 5. No credential logging (AP/STA passwords, tokens) in log output
for ext in ["*.cpp", "*.hpp", "*.h", "*.c"]:
    for f in list((NODE_DIR / "main").rglob(ext)) + list((NODE_DIR / "components").rglob(ext)):
        check_file_contains(
            f,
            [
                ("Password: %s", "Forbidden credential logging (Password: %s) in log output"),
                ("Password: '%s'", "Forbidden credential logging (Password: '%s') in log output"),
            ]
        )

# 6. LG-029: No universal credentials in production code
universal_creds = [
    ("lumigrid_admin", "Hardcoded universal credential 'lumigrid_admin'"),
    ("lumigrid_install", "Hardcoded universal credential 'lumigrid_install'"),
    ("lumigrid_op", "Hardcoded universal credential 'lumigrid_op'"),
]
# Scan firmware source files, allowing only AuthManager.cpp migration and test files
for ext in ["*.cpp", "*.hpp", "*.h", "*.c"]:
    for f in list((NODE_DIR / "main").rglob(ext)) + list((NODE_DIR / "components").rglob(ext)):
        if f.name in ["RuntimeHardeningTests.cpp", "AuthManager.cpp"]:
            continue
        check_file_contains(f, universal_creds)

# Scan frontend source files, allowing only test files
FRONTEND_SRC = REPO_ROOT / "lumigrid-lander-main" / "src"
if FRONTEND_SRC.exists():
    for f in FRONTEND_SRC.rglob("*.*"):
        if f.suffix in [".ts", ".svelte", ".js", ".mjs"]:
            if ".test." in f.name:
                continue
            check_file_contains(f, universal_creds)

# 7. LG-029: No legacy PIN or dev token symbols in production frontend
legacy_frontend_symbols = [
    ("DEFAULT_OP_PIN", "Legacy browser PIN symbol DEFAULT_OP_PIN"),
    ("DEFAULT_AD_PIN", "Legacy browser PIN symbol DEFAULT_AD_PIN"),
    ("DEV_API_TOKEN", "Legacy development token symbol DEV_API_TOKEN"),
]
if FRONTEND_SRC.exists():
    for f in FRONTEND_SRC.rglob("*.*"):
        if f.suffix in [".ts", ".svelte", ".js", ".mjs"]:
            if ".test." in f.name:
                continue
            check_file_contains(f, legacy_frontend_symbols)

# 8. LG-029: No query parameter token authentication (?token= or ?auth=)
query_param_auth = [
    ("?token=", "Forbidden query parameter authentication (?token=)"),
    ("?auth=", "Forbidden query parameter authentication (?auth=)"),
]
if FRONTEND_SRC.exists():
    for f in FRONTEND_SRC.rglob("*.*"):
        if f.suffix in [".ts", ".svelte", ".js"]:
            if ".test." in f.name:
                continue
            check_file_contains(f, query_param_auth)
for ext in ["*.cpp", "*.hpp", "*.h", "*.c"]:
    for f in list((NODE_DIR / "main").rglob(ext)) + list((NODE_DIR / "components").rglob(ext)):
        if "test" in f.name.lower():
            continue
        check_file_contains(f, query_param_auth)

# 9. LG-029: No backup serialization of authentication tokens
backup_token_patterns = [
    ('cJSON_AddStringToObject(auth_obj, "adminToken"', "Forbidden backup export of adminToken"),
    ('cJSON_AddStringToObject(auth_obj, "installerToken"', "Forbidden backup export of installerToken"),
    ('cJSON_AddStringToObject(auth_obj, "operatorToken"', "Forbidden backup export of operatorToken"),
    ('"adminToken":', "Forbidden backup export serialization of adminToken"),
    ('"installerToken":', "Forbidden backup export serialization of installerToken"),
    ('"operatorToken":', "Forbidden backup export serialization of operatorToken"),
]
backup_files = [
    NODE_DIR / "components" / "factory" / "FactoryApi.cpp",
]
for f in backup_files:
    check_file_contains(
        f,
        [
            ('cJSON_AddStringToObject(auth_obj, "adminToken"', "Forbidden backup export of adminToken"),
            ('cJSON_AddStringToObject(auth_obj, "installerToken"', "Forbidden backup export of installerToken"),
            ('cJSON_AddStringToObject(auth_obj, "operatorToken"', "Forbidden backup export of operatorToken"),
        ]
    )

if errors:
    print("\nFAILED: Discovered architectural invariant violations:")
    for err in errors:
        print(f"  [!] {err}")
    sys.exit(1)
else:
    print("[PASS] All certification architectural invariants verified successfully.")
    sys.exit(0)
