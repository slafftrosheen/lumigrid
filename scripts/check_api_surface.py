#!/usr/bin/env python3
"""Audit the external dashboard API surface without requiring a build.

This is intentionally a report-only check during the API migration phase.
It identifies legacy device endpoints still referenced by the Svelte client and
summarises canonical /api/v1 usage. It exits non-zero only for malformed input,
so developers can use it locally while the migration is in progress.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API_FILE = ROOT / "lumigrid-lander-main" / "src" / "lib" / "api" / "node.ts"

LEGACY = re.compile(r"['\"](/(?:json|edit|update|animations\.json)(?:/|['\"])?)")
CANONICAL = re.compile(r"['\"](/api/v1/[^'\"]+)")


def main() -> int:
    if not API_FILE.exists():
        print(f"ERROR: API client not found: {API_FILE}", file=sys.stderr)
        return 2

    text = API_FILE.read_text(encoding="utf-8")
    legacy = sorted(set(m.group(1) for m in LEGACY.finditer(text)))
    canonical = sorted(set(m.group(1) for m in CANONICAL.finditer(text)))

    print("LumiGrid API surface audit")
    print(f"client: {API_FILE.relative_to(ROOT)}")
    print()
    print(f"canonical /api/v1 references: {len(canonical)}")
    for path in canonical:
        print(f"  + {path}")

    print()
    print(f"legacy/device-local references: {len(legacy)}")
    for path in legacy:
        print(f"  ! {path}")

    print()
    if legacy:
        print("ACTION: migrate or explicitly isolate these compatibility endpoints.")
    else:
        print("OK: no legacy device endpoints found in the TypeScript API client.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
