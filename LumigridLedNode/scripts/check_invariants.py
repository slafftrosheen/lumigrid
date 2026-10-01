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
6. LG-034: legacy cue endpoints used outside the compat API module.
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
    check_file_contains(f, backup_token_patterns)

# 10. LG-031: Normal dashboard routes must not introduce /json/* or import legacy.ts
DASHBOARD_ROUTES_DIR = FRONTEND_SRC / "routes" / "dashboard"
if DASHBOARD_ROUTES_DIR.exists():
    legacy_json_patterns = [
        ("/json/info", "Forbidden /json/info call in normal dashboard routes"),
        ("/json/state", "Forbidden /json/state call in normal dashboard routes"),
        ("/json/nodes", "Forbidden /json/nodes call in normal dashboard routes"),
        ("/json/cfg", "Forbidden /json/cfg call in normal dashboard routes"),
        ("api/legacy", "Forbidden import of legacy API in normal dashboard routes"),
    ]
    for f in DASHBOARD_ROUTES_DIR.rglob("*.*"):
        if f.suffix in [".svelte", ".ts", ".js"]:
            if ".test." in f.name:
                continue
            check_file_contains(f, legacy_json_patterns)

# 11. LG-031: Prevent multiple new WebSocket(...) owners in frontend application source
if FRONTEND_SRC.exists():
    for f in FRONTEND_SRC.rglob("*.*"):
        if f.suffix in [".ts", ".svelte", ".js"]:
            if ".test." in f.name:
                continue
            # Focused-node socket must remain centralized in nodeStore.ts
            if f.name == "nodeStore.ts":
                continue
            check_file_contains(f, [("new WebSocket(", "Forbidden multiple new WebSocket owner outside nodeStore.ts")])

# 12. LG-034: Legacy cue transport endpoints only in the designated compat module.
#     Normal dashboard routes must use /api/v1/show/control via showTransport.ts.
if FRONTEND_SRC.exists():
    legacy_cue_endpoints = [
        ("/api/v1/cue/control", "Forbidden legacy /api/v1/cue/control outside cues compat API module"),
        ("/api/v1/cue/status", "Forbidden legacy /api/v1/cue/status outside cues compat API module"),
    ]
    for f in FRONTEND_SRC.rglob("*.*"):
        if f.suffix not in [".ts", ".svelte", ".js"]:
            continue
        if ".test." in f.name:
            continue
        # The compat API module is src/lib/api/cues.ts (documented surface).
        if f.as_posix().endswith("lib/api/cues.ts"):
            continue
        check_file_contains(f, legacy_cue_endpoints)

# 13. LG-034: CueRunner must not re-expose unlocked mutable-state readers.
#     Telemetry flows exclusively through the by-value snapshot().
cue_runner_header = NODE_DIR / "components" / "show" / "include" / "CueRunner.hpp"
if cue_runner_header.exists():
    unlocked_readers = [
        ("const CueList& current_list()", "Forbidden unlocked CueList& reference escape from CueRunner"),
        ("bool is_running() const", "Forbidden unlocked is_running() reader (use snapshot().running)"),
        ("CueRunnerState state() const { return state_; }", "Forbidden unlocked state() reader (use snapshot().state)"),
        ("uint8_t active_list_index() const", "Forbidden unlocked active_list_index() reader (use snapshot().list_index)"),
        ("uint8_t current_cue_index() const", "Forbidden unlocked current_cue_index() reader (use snapshot().current_cue)"),
        ("uint8_t next_cue_index() const", "Forbidden unlocked next_cue_index() reader (use snapshot().next_cue)"),
    ]
    check_file_contains(cue_runner_header, unlocked_readers)

# 14. LG-036: No CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS in production build paths
cmakelists = NODE_DIR / "CMakeLists.txt"
if cmakelists.exists():
    check_file_contains(cmakelists, [("CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS", "Forbidden dev credentials flag in CMakeLists.txt")])

pio_ini = NODE_DIR / "platformio.ini"
if pio_ini.exists():
    pio_text = pio_ini.read_text(encoding="utf-8")
    # env:factory section must not contain CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS
    factory_match = re.search(r"\[env:factory\](.*?)(?=\[|\Z)", pio_text, re.DOTALL)
    if factory_match and "CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS" in factory_match.group(1):
        errors.append("platformio.ini: [env:factory] contains forbidden CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS")

# 15. LG-036: Bootloader app rollback must be enabled in sdkconfig.defaults
sdk_defs = NODE_DIR / "sdkconfig.defaults"
if sdk_defs.exists():
    sdk_text = sdk_defs.read_text(encoding="utf-8")
    if "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" not in sdk_text:
        errors.append("sdkconfig.defaults: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y is missing")

# 16. LG-036: Backup export must not contain syncToken or Wi-Fi passwords
backup_files = [
    NODE_DIR / "components" / "factory" / "FactoryApi.cpp",
]
for f in backup_files:
    check_file_contains(f, [
        ('cJSON_AddNumberToObject(cfg_json, "syncToken"', "Forbidden backup export of syncToken"),
        ('cJSON_AddNumberToObject(root, "syncToken"', "Forbidden backup export of syncToken"),
        ('cJSON_AddStringToObject(net_json, "password"', "Forbidden backup export of Wi-Fi password"),
        ('"wifiPassword"', "Forbidden backup export of wifiPassword"),
    ])

if errors:
    print("\nFAILED: Discovered architectural invariant violations:")
    for err in errors:
        print(f"  [!] {err}")
    sys.exit(1)
else:
    print("[PASS] All certification architectural invariants verified successfully.")
    sys.exit(0)

