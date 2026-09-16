#!/usr/bin/env python3
"""Audit the dashboard's browser state and device API surface.

Strict mode distinguishes:
1. intentional compatibility code in lib/api/legacy.ts and dedicated OTA transport in lib/api/firmware.ts;
2. intentional shared storage implementation in lib/auth/storage.ts;
3. single WebSocket client in lib/state/nodeStore.ts;
4. forbidden route/component access.

Output matches the required boundary report:
storage boundary: OK
legacy API boundary: OK / explicit compatibility
websocket boundary: OK
route-level legacy access: 0
route-level direct browser storage: 0
route-level direct WebSocket: 0
"""

from __future__ import annotations

import io
import re
import sys
from pathlib import Path

# Ensure UTF-8 output on all platforms
if sys.stdout.encoding != "utf-8":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
if sys.stderr.encoding != "utf-8":
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
DASHBOARD_SRC = ROOT / "lumigrid-lander-main" / "src"
API_DIR = DASHBOARD_SRC / "lib" / "api"
LEGACY_COMPAT_FILE = API_DIR / "legacy.ts"
FIRMWARE_API_FILE = API_DIR / "firmware.ts"
STORAGE_FILE = DASHBOARD_SRC / "lib" / "auth" / "storage.ts"
WEBSOCKET_FILE = DASHBOARD_SRC / "lib" / "state" / "nodeStore.ts"
ROUTES_DIR = DASHBOARD_SRC / "routes"

LEGACY = re.compile(r"['\"](/(?:json|edit|update|animations\.json)(?:/[^'\"\s]*)?)['\"]")
CANONICAL = re.compile(r"['\"](/api/v1/[^'\"]+)['\"]")
LOCAL_STORAGE = re.compile(r"\blocalStorage\b")
SESSION_STORAGE = re.compile(r"\bsessionStorage\b")
WEBSOCKET = re.compile(r"\bnew\s+WebSocket\b")


def scan_source_files(pattern: re.Pattern[str], exclude: set[Path] | None = None) -> list[tuple[Path, int, str]]:
    matches: list[tuple[Path, int, str]] = []
    exclude_paths = {p.resolve() for p in (exclude or set())}
    for path in sorted(DASHBOARD_SRC.rglob("*.svelte")) + sorted(DASHBOARD_SRC.rglob("*.ts")):
        if path.resolve() in exclude_paths or path.name.endswith(".test.ts") or path.name.endswith(".spec.ts"):
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError:
            continue
        for number, line in enumerate(lines, 1):
            stripped = line.strip()
            # Ignore comments that simply mention words
            if stripped.startswith("//") or stripped.startswith("*") or stripped.startswith("/*"):
                continue
            if pattern.search(line):
                matches.append((path, number, stripped))
    return matches


def main() -> int:
    strict = "--strict" in sys.argv[1:]

    if not API_DIR.exists():
        print(f"ERROR: API directory not found: {API_DIR}", file=sys.stderr)
        return 2

    canonical_endpoints = set()
    for p in API_DIR.glob("*.ts"):
        if not p.name.endswith(".test.ts") and not p.name.endswith(".spec.ts"):
            text = p.read_text(encoding="utf-8")
            canonical_endpoints.update(m.group(1) for m in CANONICAL.finditer(text))
    sorted_canonical = sorted(canonical_endpoints)

    # Direct browser storage scan
    all_storage = scan_source_files(LOCAL_STORAGE, exclude={STORAGE_FILE}) + scan_source_files(SESSION_STORAGE, exclude={STORAGE_FILE})
    route_storage = [m for m in all_storage if ROUTES_DIR.resolve() in m[0].resolve().parents or m[0].resolve() == ROUTES_DIR.resolve()]

    # Legacy endpoint scan
    all_legacy = scan_source_files(LEGACY)
    compat_legacy = [
        m for m in all_legacy
        if m[0].resolve() == LEGACY_COMPAT_FILE.resolve() or (m[0].resolve() == FIRMWARE_API_FILE.resolve() and "/update" in m[2])
    ]
    route_legacy = [m for m in all_legacy if ROUTES_DIR.resolve() in m[0].resolve().parents or m[0].resolve() == ROUTES_DIR.resolve()]
    uncontained_legacy = [
        m for m in all_legacy
        if m[0].resolve() != LEGACY_COMPAT_FILE.resolve() and not (m[0].resolve() == FIRMWARE_API_FILE.resolve() and "/update" in m[2])
    ]

    # WebSocket construction scan
    all_websockets = scan_source_files(WEBSOCKET, exclude={WEBSOCKET_FILE})
    route_websockets = [m for m in all_websockets if ROUTES_DIR.resolve() in m[0].resolve().parents or m[0].resolve() == ROUTES_DIR.resolve()]

    print("LumiGrid dashboard surface audit")
    print(f"source: {DASHBOARD_SRC.relative_to(ROOT)}")
    print(f"mode: {'strict' if strict else 'report-only'}")
    print()

    print(f"canonical /api/v1 references in API client modules: {len(sorted_canonical)}")
    for ep in sorted_canonical:
        print(f"  + {ep}")
    print()

    print(f"intentional compatibility legacy endpoints: {len(compat_legacy)}")
    for path, line, text in compat_legacy:
        print(f"  ~ {path.relative_to(ROOT)}:{line}: {text}")
    print()

    if uncontained_legacy:
        print(f"legacy/device-local references outside compatibility layer: {len(uncontained_legacy)}")
        for path, line, text in uncontained_legacy:
            print(f"  ! {path.relative_to(ROOT)}:{line}: {text}")
        print()

    if all_storage:
        print(f"direct browser storage references outside storage.ts: {len(all_storage)}")
        for path, line, text in all_storage:
            print(f"  ! {path.relative_to(ROOT)}:{line}: {text}")
        print()

    if all_websockets:
        print(f"direct WebSocket constructions outside nodeStore.ts: {len(all_websockets)}")
        for path, line, text in all_websockets:
            print(f"  ! {path.relative_to(ROOT)}:{line}: {text}")
        print()

    storage_ok = len(route_storage) == 0 and len(all_storage) == 0
    legacy_ok = len(route_legacy) == 0 and len(uncontained_legacy) == 0
    websocket_ok = len(all_websockets) == 0

    print("--- Summary Report ---")
    print(f"storage boundary: {'OK' if storage_ok else 'FAIL'}")
    print(f"legacy API boundary: {'OK / explicit compatibility' if legacy_ok else 'FAIL: uncontained legacy calls'}")
    print(f"websocket boundary: {'OK' if websocket_ok else 'FAIL: uncontained WebSocket creation'}")
    print(f"route-level legacy access: {len(route_legacy)}")
    print(f"route-level direct browser storage: {len(route_storage)}")
    print(f"route-level direct WebSocket: {len(route_websockets)}")
    print()

    if strict:
        if not storage_ok or not legacy_ok or not websocket_ok:
            print("ACTION: eliminate route-level storage/legacy/websocket access or contain inside designated boundaries.")
            return 1

    print("OK: audit complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
