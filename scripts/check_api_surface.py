#!/usr/bin/env python3
"""Audit the dashboard's browser state and device API surface.

Report-only during the migration. This deliberately scans the whole dashboard
source tree so new pages cannot quietly reintroduce direct browser storage or
legacy device endpoints outside the central API client.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DASHBOARD_SRC = ROOT / "lumigrid-lander-main" / "src"
API_FILE = DASHBOARD_SRC / "lib" / "api" / "node.ts"

LEGACY = re.compile(r"['\"](/(?:json|edit|update|animations\.json)(?:/|['\"])?[^'\"]*)")
CANONICAL = re.compile(r"['\"](/api/v1/[^'\"]+)")
LOCAL_STORAGE = re.compile(r"\blocalStorage\b")
SESSION_STORAGE = re.compile(r"\bsessionStorage\b")


def scan_files(pattern: re.Pattern[str]) -> list[tuple[str, int, str]]:
    matches: list[tuple[str, int, str]] = []
    for path in sorted(DASHBOARD_SRC.rglob("*.svelte")) + sorted(DASHBOARD_SRC.rglob("*.ts")):
        if "/lib/auth/storage.ts" in path.as_posix():
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError:
            continue
        for number, line in enumerate(lines, 1):
            if pattern.search(line):
                matches.append((str(path.relative_to(ROOT)), number, line.strip()))
    return matches


def main() -> int:
    if not API_FILE.exists():
        print(f"ERROR: API client not found: {API_FILE}", file=sys.stderr)
        return 2

    api_text = API_FILE.read_text(encoding="utf-8")
    canonical = sorted(set(m.group(1) for m in CANONICAL.finditer(api_text)))
    legacy = sorted(set(m.group(1) for m in LEGACY.finditer(api_text)))
    browser_storage = scan_files(LOCAL_STORAGE) + scan_files(SESSION_STORAGE)
    dashboard_legacy = scan_files(LEGACY)

    print("LumiGrid dashboard surface audit")
    print(f"source: {DASHBOARD_SRC.relative_to(ROOT)}")
    print()

    print(f"canonical /api/v1 references in API client: {len(canonical)}")
    for path in canonical:
        print(f"  + {path}")

    print()
    print(f"legacy/device-local references in API client: {len(legacy)}")
    for path in legacy:
        print(f"  ! {path}")

    print()
    print(f"direct browser storage references outside storage.ts: {len(browser_storage)}")
    for path, line, text in browser_storage:
        print(f"  ! {path}:{line}: {text}")

    print()
    print(f"legacy/device-local references outside API client: {len(dashboard_legacy)}")
    for path, line, text in dashboard_legacy:
        print(f"  ! {path}:{line}: {text}")

    print()
    if legacy or browser_storage or dashboard_legacy:
        print("ACTION: migrate remaining legacy/state access or explicitly isolate compatibility code.")
    else:
        print("OK: dashboard storage and device API access are centralized.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
