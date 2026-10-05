#!/usr/bin/env python3
"""Bound linked boot frames and report direct-call stack paths, not target proof."""
import argparse
import re
import subprocess

# Bounds include cold boot, legacy configuration and certification scratch.
BUDGETS = {
    "app_main": (r"app_main$", 256),
    "runtime_init": (r"lg::LumigridRuntime::init\(\)$", 1024),
    "runtime_start": (r"lg::LumigridRuntime::start\(\)$", 1024),
    "network_start": (r"lg::LumigridRuntime::start_network_services\(\)$", 1024),
    "api_start": (r"lg::FactoryApi::start\(.*", 1024),
    "scene_init": (r"lg::SceneManager::init\(\)$", 512),
    "config_load": (r"lg::ConfigStore::load_config\(.*", 512),
    "scene_load": (r"lg::ConfigStore::load_scene\(.*", 512),
    "scene_save": (r"lg::ConfigStore::save_scene\(.*", 512),
    "cert_service_init": (r"lg::CertificationService::init\(.*", 512),
    "cert_init": (r"lg::CertificationStore::init\(\)$", 1024),
    "cert_load": (r"lg::CertificationStore::load\(.*", 1024),
    "cert_save": (r"lg::CertificationStore::save\(.*", 1024),
}


def parse_assembly(assembly):
    nodes = {}
    current = None
    for line in assembly.splitlines():
        symbol = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
        if symbol:
            current = symbol.group(1)
            nodes[current] = {"name": symbol.group(2), "frame": None, "calls": set()}
        if current is None:
            continue
        frame = re.search(r"\bentry\s+a1,\s+(0x[0-9a-f]+|[0-9]+)", line)
        if frame:
            nodes[current]["frame"] = int(frame.group(1), 0)
        call = re.search(r"\bcall(?:0|4|8|12)\s+([0-9a-f]+) <", line)
        if call:
            nodes[current]["calls"].add(call.group(1))
    return nodes


def check_frames(nodes):
    results = []
    for label, (pattern, budget) in BUDGETS.items():
        matches = [n for n in nodes.values() if re.match(pattern, n["name"])]
        # Lambda frames are checked too; a missing function entry is a failure.
        if not matches or any(n["frame"] is None for n in matches):
            raise ValueError(f"Missing stack evidence for {label}")
        size = max(n["frame"] for n in matches)
        if size > budget:
            raise ValueError(f"boot function {label} stack frame {size} exceeds {budget}-byte budget")
        results.append(f"[PASS] boot function {label}: {size}-byte stack frame")
    return results


def startup_path(nodes, root, visiting=frozenset()):
    """Longest acyclic direct-call path; indirect and recursive depth need target evidence."""
    if root in visiting or root not in nodes:
        return 0, []
    node = nodes[root]
    best = (0, [])
    for child in node["calls"]:
        path = startup_path(nodes, child, visiting | {root})
        if path[0] > best[0]:
            best = path
    return (node["frame"] or 0) + best[0], [root] + best[1]


def check_startup_path(nodes):
    root = next((k for k, n in nodes.items() if n["name"] == "app_main"), None)
    if root is None:
        raise ValueError("Missing app_main call-path evidence")
    size, path = startup_path(nodes, root)
    # Leave at least 25% of the unchanged 10240-byte main stack for paths not
    # represented here. Target high-water marks remain the acceptance gate.
    if size > 7680:
        chain = " -> ".join(nodes[k]["name"] for k in path)
        raise ValueError(f"boot direct-call path {size} exceeds 7680-byte budget: {chain}")
    return f"[PASS] boot acyclic direct-call path: {size} bytes; indirect/ROM/interrupt usage requires target validation"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--elf", required=True)
    args = parser.parse_args()
    assembly = subprocess.check_output([args.objdump, "-dC", args.elf], text=True)
    try:
        for result in check_frames(parse_assembly(assembly)):
            print(result)
        print(check_startup_path(parse_assembly(assembly)))
    except ValueError as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
