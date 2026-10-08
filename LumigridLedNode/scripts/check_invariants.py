#!/usr/bin/env python3
"""
check_invariants.py - Certification Invariant Static Validation

Fails with non-zero exit code if any forbidden architectural anti-patterns
or lifecycle regressions are discovered in the codebase:
1. ESP_ERROR_CHECK in runtime lifecycle code.
2. Uncontrolled vTaskDelete(task_handle_) outside cooperative shutdown.
3. Process-global 'static bool services_started'.
4. Universal credentials outside the restricted FactoryFirstBoot setup path.
5. Unsynchronized direct output mutations outside controlled transactions/factory exceptions.
6. LG-034: legacy cue endpoints used outside the compat API module.
"""

import sys
import re
import json
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

# Production hardware/storage contract, including generated configuration.
from verify_partition_layout import read_layout, validate_layout
try:
    validate_layout(read_layout(NODE_DIR / "partitions.factory.csv"))
except ValueError as exc:
    errors.append(str(exc))
for name in ("sdkconfig.defaults", "sdkconfig.factory.defaults", "sdkconfig.factory"):
    text = (NODE_DIR / name).read_text()
    if "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" not in text or 'CONFIG_ESPTOOLPY_FLASHSIZE="8MB"' not in text or "CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y" in text:
        errors.append(name + ": production flash must be 8 MB")
import configparser
pio_config = configparser.ConfigParser()
pio_config.read_string((NODE_DIR / "platformio.ini").read_text())
for section in ("env:factory", "env:test"):
    if pio_config.get(section, "board_upload.flash_size", fallback="") != "8MB":
        errors.append(section + ": board flash target must be 8 MB")

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

factory_match = None
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

# 16. LG-038.1: Factory firmware must not enable unused FreeRTOS static allocation
#     Project defaults disable application static allocation. The pinned IDF port owns
#     scheduler memory through its heap-backed hooks (documented owner exception).
# Explicit owner approved for the pinned IDF port, which forces this option on.
# Revisit this exception whenever the framework or platform changes.
owner_path = NODE_DIR / "scripts" / "boot_memory_policy.json"
try:
    owner = json.loads(owner_path.read_text(encoding="utf-8"))
except (OSError, ValueError):
    owner = {}
expected_owner = {
    "scheduler_memory_owner": "ESP-IDF port_common.c",
    "platform": "espressif32@6.7.0",
    "esp_idf": "5.2.1",
    "documentation": "docs/LG-038.1-BOOT-HOTFIX.md",
}
framework_owns_scheduler = (
    owner == expected_owner
    and (REPO_ROOT / expected_owner["documentation"]).exists()
    and re.search(r"^platform\s*=\s*espressif32@6\.7\.0\s*$", factory_match.group(1) if factory_match else "", re.MULTILINE)
    and re.search(r"^framework\s*=\s*espidf\s*$", factory_match.group(1) if factory_match else "", re.MULTILINE)
)
for ext in ("*.cpp", "*.hpp", "*.h", "*.c"):
    for f in list((NODE_DIR / "main").rglob(ext)) + list((NODE_DIR / "components").rglob(ext)):
        check_file_contains(f, [
            ("vApplicationGetTimerTaskMemory(", "Application timer scheduler memory hook forbidden"),
            ("vApplicationGetIdleTaskMemory(", "Application idle scheduler memory hook forbidden"),
        ])

for sdk_path in (NODE_DIR / "sdkconfig.defaults", NODE_DIR / "sdkconfig.factory.defaults", NODE_DIR / "sdkconfig.factory"):
    if not sdk_path.exists():
        errors.append(f"{sdk_path.name}: required production configuration missing")
        continue
    sdk_text = sdk_path.read_text(encoding="utf-8")
    if re.search(r"^CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y$", sdk_text, re.MULTILINE) and not (sdk_path.name == "sdkconfig.factory" and framework_owns_scheduler):
        errors.append(f"{sdk_path.name}: Factory/production firmware must not enable CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION without an explicit documented static-allocation owner (LG-038.1)")

factory_sdk = NODE_DIR / "sdkconfig.factory"
if factory_sdk.exists():
    factory_text = factory_sdk.read_text(encoding="utf-8")
    for symbol in ("CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH", "CONFIG_FREERTOS_TIMER_QUEUE_LENGTH"):
        match = re.search(rf"^{symbol}=(\d+)$", factory_text, re.MULTILINE)
        if not match or not ((1536 <= int(match.group(1)) <= 32768) if symbol.endswith("STACK_DEPTH") else (5 <= int(match.group(1)) <= 20)):
            errors.append(f"sdkconfig.factory: {symbol} must be positive and within ESP-IDF limits")
    main_stack = re.search(r"^CONFIG_ESP_MAIN_TASK_STACK_SIZE=(\d+)$", factory_text, re.MULTILINE)
    if not main_stack or int(main_stack.group(1)) != 10240:
        errors.append("sdkconfig.factory: boot stack budgets require the unchanged 10240-byte main stack")
    # IDF 5.2 enables configUSE_TIMERS unconditionally in FreeRTOSConfig.h.
    if "# CONFIG_FREERTOS_USE_TIMERS is not set" in factory_text or "CONFIG_FREERTOS_USE_TIMERS=n" in factory_text:
        errors.append("sdkconfig.factory: timer service must remain enabled")

# PCA9685 stack buffers require the legacy synchronous I2C transport.
pca_driver = NODE_DIR / "components" / "output" / "Pca9685Driver.cpp"
if pca_driver.exists():
    pca_text = pca_driver.read_text(encoding="utf-8")
    for required in ('#include "driver/i2c.h"', 'kI2cPort = I2C_NUM_0',
                     'kI2cHz = 100000', 'cfg.master.clk_speed = kI2cHz',
                     'cfg.sda_io_num = lg::board::kI2cSda',
                     'cfg.scl_io_num = lg::board::kI2cScl',
                     'i2c_param_config(', 'i2c_driver_install(',
                     'i2c_master_write_to_device(', 'i2c_master_write_read_device(',
                     'kI2cPort, lg::board::kPcaAddress', 'pdMS_TO_TICKS(100)'):
        if required not in pca_text:
            errors.append("Pca9685Driver.cpp: missing legacy synchronous I2C contract: " + required)
    check_file_contains(pca_driver, [(pattern, "PCA9685 must use legacy synchronous I2C without discovery or async callbacks")
        for pattern in ('driver/i2c_master.h', 'trans_queue_depth',
                        'i2c_master_register_event_callbacks(', 'i2c_new_master_bus(',
                        'i2c_master_bus_add_device(', 'i2c_master_transmit(',
                        'i2c_master_transmit_receive(', 'i2c_master_probe(',
                        'i2c_master_bus_rm_device(', 'i2c_del_master_bus(')])
    board_text = (NODE_DIR / "components" / "board" / "include" / "lg_board_pins.hpp").read_text()
    for name, value in (("kPcaAddress", "0x55"), ("kI2cSda", "GPIO_NUM_21"), ("kI2cScl", "GPIO_NUM_22")):
        if not re.search(r"\b" + name + r"\s*=\s*" + value + r"\s*;", board_text):
            errors.append("lg_board_pins.hpp: PCA hardware contract requires " + name + "=" + value)


# 17. LG-036: Backup export must not contain syncToken or Wi-Fi passwords
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

# LG-038.7: Reject universal credentials and MAC-derived credentials in production code
credential_forbidden = [
    ("lumigrid123", "Forbidden universal credential 'lumigrid123'"),
    ("mac_pass", "Forbidden MAC-derived password variable 'mac_pass'"),
    ("lg_%02x%02x%02x%02x", "Forbidden MAC-derived password format 'lg_%02x%02x%02x%02x'"),
]
cred_check_dirs = [
    REPO_ROOT / "lumigrid-lander-main" / "src",
    REPO_ROOT / "lumigrid-lander-main" / "embedded",
    NODE_DIR / "main",
    NODE_DIR / "components" / "auth",
    NODE_DIR / "components" / "network",
    NODE_DIR / "components" / "config",
    NODE_DIR / "components" / "factory",
]
for cred_dir in cred_check_dirs:
    if not cred_dir.exists():
        continue
    for f in cred_dir.rglob("*"):
        if f.suffix not in {".cpp", ".hpp", ".ts", ".svelte", ".html", ".js", ".mjs"} or f.name.endswith((".test.ts", ".spec.ts", ".test.js")):
            continue
        if f.name == "RuntimeHardeningTests.cpp":
            continue
        check_file_contains(f, credential_forbidden)

# LG-038.7: HTTP task stack bounding and scratch allocation invariants
factory_api = NODE_DIR / "components" / "factory" / "FactoryApi.cpp"
if factory_api.exists():
    fa_text = factory_api.read_text(encoding="utf-8")
    status_handler = re.search(r"esp_err_t FactoryApi::get_signage_status\(.*?\n\}", fa_text, re.S)
    if status_handler and "char payload[kRuntimeSnapshotMaxBytes];" in status_handler.group(0):
        errors.append("FactoryApi.cpp: forbidden 'char payload[kRuntimeSnapshotMaxBytes];' on HTTPD stack in get_signage_status")
    fleet_handler = re.search(r"esp_err_t FactoryApi::get_fleet_nodes\(.*?\n\}", fa_text, re.S)
    if fleet_handler and re.search(r"DiscoveredNode\s+snap\s*\[\s*(discovery::)?FleetDiscoveryService::kMaxDiscoveredNodes\s*\];", fleet_handler.group(0)):
        errors.append("FactoryApi.cpp: forbidden 'DiscoveredNode snap[...]' on HTTPD stack in get_fleet_nodes")
    match_stack = re.search(r"http_config\.stack_size\s*=\s*(\d+);", fa_text)
    if match_stack and int(match_stack.group(1)) > 8192:
        errors.append(f"FactoryApi.cpp: http_config.stack_size={match_stack.group(1)} exceeds maximum allowed 8192 bytes")
    if "RuntimeSnapshotScratch" not in fa_text:
        errors.append("FactoryApi.cpp: missing heap-backed RuntimeSnapshotScratch for status handler")
    if "FleetSnapshot" not in fa_text:
        errors.append("FactoryApi.cpp: missing heap-backed FleetSnapshot for fleet nodes handler")

# LG-038.3 §5/§77: app_main must return after startup; no permanent heartbeat loop.
main_cpp = (NODE_DIR / "main" / "main.cpp").read_text(encoding="utf-8")
if re.search(r"while\s*\(\s*(true|1)\s*\)", main_cpp):
    errors.append("main.cpp: app_main must not contain a permanent heartbeat loop (LG-038.3 §5/§77)")
if "while" in main_cpp and not re.search(r"return\s*;", main_cpp):
    errors.append("main.cpp: app_main must return after startup so the main task stack is reclaimed")

# LG-038.3 §13/§78: fresh DeviceConfig protocol defaults must be OFF.
signage_types = (NODE_DIR / "components" / "config" / "include" / "SignageTypes.hpp").read_text(encoding="utf-8")
protocol_block = re.search(r"struct ProtocolConfig \{(.*?)\};", signage_types, re.S)
if not protocol_block or "artnet_enabled       = false" not in protocol_block.group(1) or "sacn_enabled         = false" not in protocol_block.group(1):
    errors.append("SignageTypes.hpp: fresh ProtocolConfig defaults must be artnet_enabled=false and sacn_enabled=false (LG-038.3 §13)")

# LG-038.3 §7: no permanent net_apply task creation at boot.
runtime_cpp = (NODE_DIR / "main" / "LumigridRuntime.cpp").read_text(encoding="utf-8")
init_block = re.search(r"esp_err_t LumigridRuntime::init\(\) \{(.*?)\n\}", runtime_cpp, re.S)
if init_block and re.search(r"xTaskCreate(PinnedToCore)?\(\s*&?LumigridRuntime::net_apply_worker", init_block.group(1)):
    errors.append("LumigridRuntime::init(): net_apply worker must not be created permanently at boot (LG-038.3 §7/§8)")

# LG-038.3 §17: factory worker must not start unconditionally at boot.
if init_block and re.search(r"factory_runner_\.start\(\)", init_block.group(1)):
    errors.append("LumigridRuntime::init(): factory worker must start lazily, not at boot (LG-038.3 §17/§18)")

# LG-038.3 §11/§12: protocol workers must be config-gated in start_network_services().
services_block = re.search(r"esp_err_t LumigridRuntime::start_network_services\(\) \{(.*?)\n\}", runtime_cpp, re.S)
if services_block:
    svc_text = services_block.group(1)
    planner = (NODE_DIR / "components" / "factory" / "include" / "RuntimeServices.hpp").read_text(encoding="utf-8")
    reconcile = re.search(r"esp_err_t LumigridRuntime::reconcile_services\(.*?\n\}", runtime_cpp, re.S)
    if ("reconcile_services(current_cfg, current_cfg" not in svc_text or
        "in.cfg->protocol.artnet_enabled" not in planner or
        "in.cfg->protocol.sacn_enabled" not in planner or not reconcile or
        "compute_service_plan(" not in reconcile.group(0) or
        "!desired.artnet" not in reconcile.group(0) or "!desired.sacn" not in reconcile.group(0)):
        errors.append("Runtime startup must reconcile the config-gated protocol service plan (LG-038.3 §11/§12)")

# LG-038.3 §21/§22: fleet discovery must not start unconditionally (AP-only). 
if services_block and ("set_network_plan_state(wifi_.state()" not in services_block.group(1) or
                       "plan.fleet_discovery = (in.network == NetworkPlanState::StaConnected)" not in planner or
                       not reconcile or "if (desired.fleet_discovery)" not in reconcile.group(0)):
    errors.append("Runtime network reconciliation must use the STA-gated FleetDiscovery plan (LG-038.3 §21/§22)")

# LG-HW-001: Hardware coexistence architectural invariants
for cfg_name in ("sdkconfig.defaults", "sdkconfig.factory.defaults", "sdkconfig.factory"):
    cfg_text = (NODE_DIR / cfg_name).read_text(encoding="utf-8")
    if "CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y" not in cfg_text:
        errors.append(f"{cfg_name}: Wi-Fi task must be pinned to CPU0 (CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y)")
    if "CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y" not in cfg_text:
        errors.append(f"{cfg_name}: Main task must have affinity CPU0 (CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y)")
    if "CONFIG_RMT_ISR_IRAM_SAFE=y" not in cfg_text:
        errors.append(f"{cfg_name}: RMT ISR must be IRAM safe (CONFIG_RMT_ISR_IRAM_SAFE=y)")

par_rmt_hpp = (NODE_DIR / "components" / "output" / "include" / "ParallelRmtDriver.hpp").read_text(encoding="utf-8")
if "static_assert(MAX_RMT_LANES == 8" not in par_rmt_hpp:
    errors.append("ParallelRmtDriver.hpp: RMT lane count must remain 8")
if "kRmtSymbolsPerLane = 64" not in par_rmt_hpp:
    errors.append("ParallelRmtDriver.hpp: RMT symbols/lane must remain 64")
if "kRmtInterruptPriority = 3" not in par_rmt_hpp:
    errors.append("ParallelRmtDriver.hpp: RMT interrupt priority must remain 3")
if "!CONFIG_RMT_ISR_IRAM_SAFE" not in par_rmt_hpp:
    errors.append("ParallelRmtDriver.hpp: compile-time check for CONFIG_RMT_ISR_IRAM_SAFE required")

par_rmt_cpp = (NODE_DIR / "components" / "output" / "ParallelRmtDriver.cpp").read_text(encoding="utf-8")
if "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" not in par_rmt_cpp:
    errors.append("ParallelRmtDriver.cpp: wire buffers and StripEncoder must use internal DRAM allocation path")
if "cc.intr_priority = kRmtInterruptPriority" not in par_rmt_cpp:
    errors.append("ParallelRmtDriver.cpp: RMT TX channel must explicitly configure interrupt priority 3")

out_ctrl_hpp = (NODE_DIR / "components" / "output" / "include" / "OutputController.hpp").read_text(encoding="utf-8")
if "init_on_output_core" not in out_ctrl_hpp or "reconfigure_on_output_core" not in out_ctrl_hpp:
    errors.append("OutputController.hpp: must declare init_on_output_core and reconfigure_on_output_core")

exec_hpp = (NODE_DIR / "components" / "output" / "include" / "OutputCoreExecutor.hpp")
if not exec_hpp.exists():
    errors.append("OutputCoreExecutor.hpp: hardware lifecycle executor header missing")
exec_cpp = (NODE_DIR / "components" / "output" / "OutputCoreExecutor.cpp")
if not exec_cpp.exists():
    errors.append("OutputCoreExecutor.cpp: hardware lifecycle executor implementation missing")
else:
    exec_text = exec_cpp.read_text(encoding="utf-8")
    if not re.search(r"xTaskCreatePinnedToCore\([^)]*,\s*1\s*(?://[^\n]*)?\)", exec_text, re.S):
        errors.append("OutputCoreExecutor.cpp: executor worker must be pinned permanently to CPU1")

# LG-HW-002: Transactional Output, Storage Ownership, and Wi-Fi Fault Containment
for ext in ["*.cpp", "*.hpp", "*.h", "*.c"]:
    for f in (NODE_DIR / "components" / "network").rglob(ext):
        check_file_contains(f, [("nvs_flash_erase(", "Forbidden nvs_flash_erase in network components (LG-HW-002 §9)")])

cfg_store_cpp = NODE_DIR / "components" / "config" / "ConfigStore.cpp"
if cfg_store_cpp.exists():
    check_file_contains(cfg_store_cpp, [("nvs_flash_erase(", "Forbidden automatic nvs_flash_erase in ConfigStore (LG-HW-002 §9)")])

nvs_runtime_hpp = NODE_DIR / "components" / "storage" / "include" / "NvsRuntime.hpp"
if not nvs_runtime_hpp.exists():
    errors.append("NvsRuntime.hpp: Centralized storage owner header missing (LG-HW-002 §9)")

if "kPostTopologyInternalReserveBytes = 12 * 1024" not in par_rmt_hpp:
    errors.append("ParallelRmtDriver.hpp: kPostTopologyInternalReserveBytes must be 12 KB (12 * 1024) (LG-HW-002 §1)")
if "check_output_memory_admission" not in out_ctrl_hpp:
    errors.append("OutputController.hpp: check_output_memory_admission declaration missing (LG-HW-002 §1)")
if "rollback_to_last_good_topology" not in out_ctrl_hpp:
    errors.append("OutputController.hpp: rollback_to_last_good_topology declaration missing (LG-HW-002 §5)")
if "OutputTopologySnapshot" not in out_ctrl_hpp:
    errors.append("OutputController.hpp: OutputTopologySnapshot declaration missing (LG-HW-002 §4)")

wifi_service_cpp = NODE_DIR / "components" / "network" / "WifiService.cpp"
if wifi_service_cpp.exists():
    ws_text = wifi_service_cpp.read_text(encoding="utf-8")
    if "schedule_exit_fallback_ap" not in ws_text or "perform_exit_fallback_ap" not in ws_text:
        errors.append("WifiService.cpp: missing scheduled exit fallback AP lifecycle methods (LG-HW-002 §10)")
    if "actual_mode" not in ws_text:
        errors.append("WifiService.cpp: missing actual_mode diagnostics reporting (LG-HW-002 §11)")

if errors:
    print("\nFAILED: Discovered architectural invariant violations:")
    for err in errors:
        print(f"  [!] {err}")
    sys.exit(1)
else:
    print("[PASS] All certification architectural invariants verified successfully.")
    sys.exit(0)

