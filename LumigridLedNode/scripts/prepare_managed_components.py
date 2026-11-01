#!/usr/bin/env python3
"""Preserve the known incomplete cJSON checkout; verify restored locked inputs.

Run with ESP-IDF's Python before/after project() configures managed components.
Unknown changes are never removed, repaired, or assigned a replacement hash.
"""
import argparse
from pathlib import Path
import re
import sys
import tempfile

import yaml
from idf_component_tools.hash_tools.calculate import hash_dir
from idf_component_tools.manifest import ManifestManager


COMPONENT = "espressif__cjson"
REGISTRY_HASH = "e788323270d90738662d66fffa910bfe1fba019bba087f01557e70c40485b469"
CHECKOUT_HASH = "5c5d9ffbaec248ee7ddfc67c688235e477038c756add8b84d14a1846fa6f65d2"
REGISTRY_VERSION = "1.7.19~2"


def locked_component(node):
    lock = yaml.safe_load((node / "dependencies.lock").read_text(encoding="utf-8"))
    component = lock["dependencies"]["espressif/cjson"]
    if (component.get("source", {}).get("type") != "service"
            or not re.fullmatch(r"[a-f0-9]{64}", component.get("component_hash", ""))
            or not component.get("version")):
        raise ValueError("cJSON requires a registry version and SHA256 in dependencies.lock")
    return component


def component_hash(directory):
    # Match the pinned manager's managed-component validation, including manifest
    # include/exclude patterns and .git files. Git ignores/default excludes do not apply.
    if directory.is_symlink() or any(path.is_symlink() for path in directory.rglob("*")):
        raise ValueError(f"Linked managed-component inputs are not supported: {directory}")
    manifest = ManifestManager(str(directory / "idf_component.yml"), "cmp").load()
    return hash_dir(directory, include=set(manifest.files["include"]),
                    exclude=set(manifest.files["exclude"]) | {"**/.component_hash"},
                    exclude_default=False)


def prepare(node):
    locked = locked_component(node)
    directory = node / "managed_components" / COMPONENT
    if not directory.exists() and not directory.is_symlink():
        return None  # The component manager will fetch the locked package.
    actual = component_hash(directory)
    marker = directory / ".component_hash"
    recorded = marker.read_text(encoding="utf-8").strip() if marker.is_file() else None
    if actual == locked["component_hash"] and recorded in (None, actual):
        return None  # A complete locked package needs no checkout restoration.
    if (locked["version"] != REGISTRY_VERSION or locked["component_hash"] != REGISTRY_HASH
            or recorded != REGISTRY_HASH or actual != CHECKOUT_HASH):
        raise ValueError(f"cJSON differs from the recognized checkout; preserved untouched: {directory}")

    snapshots = node / ".pio" / "dependency-snapshots"
    snapshots.mkdir(parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix="cjson-", dir=snapshots)) / COMPONENT
    directory.rename(backup)
    print(f"Preserved incomplete cJSON checkout at {backup}; restoring locked registry package")
    return backup


def verify(node):
    locked = locked_component(node)
    directory = node / "managed_components" / COMPONENT
    actual = component_hash(directory)
    marker = (directory / ".component_hash").read_text(encoding="utf-8").strip()
    if actual != locked["component_hash"] or marker != locked["component_hash"]:
        raise ValueError("Restored cJSON files/marker do not match dependencies.lock")
    print(f"Verified cJSON {locked['version']} against locked SHA256 {actual}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--node-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("action", choices=("prepare", "verify"))
    args = parser.parse_args()
    try:
        (prepare if args.action == "prepare" else verify)(args.node_root.resolve())
    except Exception as error:
        print(f"Managed dependency check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
