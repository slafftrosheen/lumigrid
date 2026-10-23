#!/usr/bin/env python3
"""Package checked factory/test binaries with provenance, never a CE pass claim.

Run after successful builds of both environments. Relocated build directories and
source copies are supported; current source inputs must match the repository.
This records current input hashes, not a cryptographic attestation of compilation.
"""
import argparse
import configparser
import hashlib
from importlib.metadata import PackageNotFoundError, version
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from verify_partition_layout import FLASH_BYTES, LAYOUT_VERSION, read_layout, validate_layout, size_health


FLASH_CONFIG = {
    "IDF_TARGET": "esp32", "ESPTOOLPY_FLASHSIZE": "8MB",
    "ESPTOOLPY_FLASHMODE": "dio", "ESPTOOLPY_FLASHFREQ": "40m",
    "PARTITION_TABLE_OFFSET": 0x8000, "BOOTLOADER_OFFSET_IN_FLASH": 0x1000,
}
SOURCE_PATHS = [
    "LumigridLedNode", "lumigrid-lander-main/embedded/dashboard.html",
    "lumigrid-lander-main/scripts/build-embedded.mjs",
    "lumigrid-lander-main/package.json", "lumigrid-lander-main/package-lock.json",
]
GENERATED_PATHS = {
    "LumigridLedNode/generated/dashboard_assets.hpp",
    "LumigridLedNode/components/factory/include/dashboard_assets.hpp",
    "LumigridLedNode/sdkconfig.factory", "LumigridLedNode/sdkconfig.test",
    "LumigridLedNode/sdkconfig.rollback-test",
}


def run(command, cwd=None):
    return subprocess.check_output([str(v) for v in command], cwd=cwd, text=True,
                                   stderr=subprocess.STDOUT).rstrip("\r\n")


def fingerprint(path, allow_empty=False):
    data = path.read_bytes()
    if not data and not allow_empty:
        raise ValueError(f"Required input is empty: {path}")
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def input_inventory(repo_root, node_root, paths):
    inventory = {}
    for relative in sorted(set(paths) - GENERATED_PATHS):
        original = fingerprint(repo_root / relative, allow_empty=True)
        build_path = (node_root / relative[len("LumigridLedNode/"):]
                      if relative.startswith("LumigridLedNode/") else node_root.parent / relative)
        copied = fingerprint(build_path, allow_empty=True)
        if original != copied:
            raise ValueError(f"Build source differs from repository: {relative}")
        inventory[relative] = original
    return inventory


def managed_source_paths(repo_root, node_root):
    def files(node):
        directory = node / "managed_components"
        if not directory.is_dir():
            raise ValueError(f"Managed component sources are missing: {directory}")
        paths = set()
        for path in directory.rglob("*"):
            if path.is_symlink() and path.is_dir():
                raise ValueError(f"Cannot inventory linked component directory: {path}")
            if path.is_file():
                paths.add("LumigridLedNode/" + path.relative_to(node).as_posix())
        if not paths:
            raise ValueError(f"Managed component sources are empty: {directory}")
        return paths

    original, copied = files(repo_root / "LumigridLedNode"), files(node_root)
    if original != copied:
        raise ValueError("Build managed component files differ from repository sources")
    return original


def source_snapshot(repo_root, node_root):
    paths = run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                 "--", *SOURCE_PATHS], cwd=repo_root).split("\0")
    # Component Manager metadata and restored package files can be ignored by
    # Git. Hash the actual dependency tree as well as the version lockfile.
    all_paths = {p for p in paths if p} | managed_source_paths(repo_root, node_root)
    inputs = input_inventory(repo_root, node_root, all_paths)
    if "LumigridLedNode/platformio.ini" not in inputs:
        raise ValueError("Repository does not contain the firmware build inputs")
    state = run(["git", "status", "--porcelain=v1", "--untracked-files=all"], cwd=repo_root)
    return {
        "commit": run(["git", "rev-parse", "HEAD"], cwd=repo_root),
        "dirty": bool(state), "working_tree_status": state.splitlines(),
        "inputs_sha256": hashlib.sha256(json.dumps(inputs, sort_keys=True,
                                                   separators=(",", ":")).encode()).hexdigest(),
        "inputs": inputs,
        "managed_dependencies": "Full managed_components tree, including Git-ignored package files, is included in inputs",
        "generated_files": "Recorded separately as build artifacts and effective configuration",
        "binding": "Current inputs match build source; successful build invocation is required",
    }


def validate_flash_config(sdkconfig, generated):
    parsed = dict(re.findall(r"^CONFIG_([A-Z0-9_]+)=(.+)$", sdkconfig, re.MULTILINE))
    for key, expected in FLASH_CONFIG.items():
        value = parsed.get(key)
        if value is None:
            raise ValueError(f"Effective sdkconfig is missing {key}")
        value = int(value, 0) if isinstance(expected, int) else value.strip('"')
        if value != expected or generated.get(key) != expected:
            raise ValueError(f"Effective configuration contradicts production {key}={expected}")


def validate_image(data, name):
    # ESP32 image headers encode DIO=2, 8 MB=3, 40 MHz=0.
    if len(data) < 24 or data[0] != 0xe9:
        raise ValueError(f"Invalid ESP32 image header: {name}")
    if data[2] != 2 or data[3] != 0x30:
        raise ValueError(f"{name} must declare DIO, 40 MHz, 8 MB flash")


def validate_merged(merged, inputs):
    if not merged or len(merged) > FLASH_BYTES:
        raise ValueError("Merged image is empty or exceeds 8 MB flash")
    end = 0
    for offset, data in sorted(inputs.items()):
        if offset < end:
            raise ValueError("Merged image input ranges overlap")
        if not data or offset + len(data) > FLASH_BYTES:
            raise ValueError("Merged image input is empty or exceeds configured flash")
        end = offset + len(data)
    end = 0
    for offset, data in sorted(inputs.items()):
        if any(b != 0xff for b in merged[end:offset]):
            raise ValueError("Merged image contains unexpected data outside input images")
        if merged[offset:offset + len(data)] != data:
            raise ValueError(f"Merged image differs from build input at {offset:#x}")
        end = offset + len(data)
    if any(b != 0xff for b in merged[end:]):
        raise ValueError("Merged image contains unexpected trailing data")


def package_candidate(repo_root, node_root, build_root, core_dir, merged, output):
    if output.exists():
        raise ValueError(f"Output already exists; choose a fresh candidate directory: {output}")
    source = source_snapshot(repo_root, node_root)
    config = configparser.ConfigParser(interpolation=None)
    config.read(node_root / "platformio.ini")
    parts_path = node_root / "partitions.factory.csv"
    parts = read_layout(parts_path)
    validate_layout(parts)
    platform = json.loads((core_dir / "platforms/espressif32/platform.json").read_text())
    artifacts = {"merged_firmware.bin": merged, "platformio.ini": node_root / "platformio.ini",
                 "partitions.factory.csv": parts_path,
                 "dependencies.lock": node_root / "dependencies.lock",
                 "dashboard_assets.hpp": node_root / "generated/dashboard_assets.hpp"}
    if fingerprint(artifacts["dashboard_assets.hpp"]) != fingerprint(
            node_root / "components/factory/include/dashboard_assets.hpp"):
        raise ValueError("Generated embedded dashboard headers disagree")
    builds = {}
    stack_report = None
    tool_packages = {"platform": platform["version"]}
    for name in ("factory", "test"):
        env = config[f"env:{name}"]
        if (env.get("board_upload.flash_size") != "8MB" or
                int(env.get("board_upload.maximum_size", "0"), 0) != FLASH_BYTES or
                env.get("board_build.partitions") != "partitions.factory.csv" or
                env.get("platform") != f"espressif32@{platform['version']}"):
            raise ValueError(f"{name}: PlatformIO configuration contradicts the production contract")
        build = build_root / name
        description_path = build / "project_description.json"
        description = json.loads(description_path.read_text())
        if (Path(description["project_path"]).resolve() != node_root or
                Path(description["build_dir"]).resolve() != build.resolve() or
                description["target"] != "esp32"):
            raise ValueError(f"{name}: build metadata points to a different project, build directory or target")
        effective_config = Path(description["config_file"])
        if effective_config.resolve() != (node_root / f"sdkconfig.{name}").resolve():
            raise ValueError(f"{name}: unexpected effective sdkconfig path")
        generated_config = build / "config/sdkconfig.json"
        validate_flash_config(effective_config.read_text(), json.loads(generated_config.read_text()))
        idf_path = Path(description["idf_path"])
        compiler = Path(description["c_compiler"])
        objdump = compiler.with_name(compiler.name.replace("gcc", "objdump"))
        idf_package = json.loads((idf_path / "package.json").read_text())
        compiler_package = json.loads((compiler.parent.parent / "package.json").read_text())
        for key, value in (("esp_idf", idf_package["version"]),
                           ("compiler_package", compiler_package["version"]),
                           ("compiler", run([compiler, "--version"]).splitlines()[0])):
            if key in tool_packages and tool_packages[key] != value:
                raise ValueError(f"Environments used different {key} versions")
            tool_packages[key] = value
        binary_paths = {"bootloader.bin": build / "bootloader.bin",
                        "partitions.bin": build / "partitions.bin", "firmware.bin": build / "firmware.bin"}
        for filename in ("bootloader.bin", "firmware.bin"):
            validate_image(binary_paths[filename].read_bytes(), f"{name}/{filename}")
        health = size_health(binary_paths["firmware.bin"].stat().st_size, parts["factory"][3])
        if health == "FAIL":
            raise ValueError(f"{name}: firmware exceeds existing 85% slot budget")
        with tempfile.TemporaryDirectory() as tmp:
            expected_table = Path(tmp) / "partitions.bin"
            run([sys.executable, idf_path / "components/partition_table/gen_esp32part.py", "-q",
                 "--offset", "0x8000", "--flash-size", "8MB", parts_path, expected_table])
            if expected_table.read_bytes() != binary_paths["partitions.bin"].read_bytes():
                raise ValueError(f"{name}: built partition binary differs from production layout")
        if name == "factory":
            stack_report = run([sys.executable, node_root / "scripts/verify_boot_stack.py",
                                "--objdump", objdump, "--elf", build / "firmware.elf"])
            validate_merged(merged.read_bytes(), {
                0x1000: binary_paths["bootloader.bin"].read_bytes(),
                0x8000: binary_paths["partitions.bin"].read_bytes(),
                parts["factory"][2]: binary_paths["firmware.bin"].read_bytes(),
            })
        for filename, path in {**binary_paths, "firmware.elf": build / "firmware.elf",
                               "sdkconfig": effective_config, "sdkconfig.json": generated_config,
                               "sdkconfig.h": build / "config/sdkconfig.h",
                               "project_description.json": description_path}.items():
            artifacts[f"{name}/{filename}"] = path
        builds[name] = {"project_version": description["project_version"],
                        "idf_revision": description["git_revision"],
                        "production_candidate": name == "factory",
                        "build_flags": env.get("build_flags", ""),
                        "slot_budget_status": health, "flash_config": FLASH_CONFIG}
    esptool = core_dir / "packages/tool-esptoolpy/esptool.py"
    tool_packages.update({"python": sys.version.split()[0], "platformio": version("platformio"),
                          "node": run(["node", "--version"]),
                          "npm": run(["npm", "--version"]).splitlines()[-1],
                          "esptool": run([sys.executable, esptool, "version"]).splitlines()[-1]})
    records = {name: fingerprint(path) for name, path in artifacts.items()}
    if source_snapshot(repo_root, node_root) != source:
        raise ValueError("Source changed while packaging; rebuild before producing a candidate")
    manifest = {
        "manifest_version": 1, "hardware_validation": "NOT_RUN",
        "compliance_assessment": "NOT_PERFORMED", "source": source,
        "tools": tool_packages, "builds": builds, "artifacts": records,
        "partition_layout_version": LAYOUT_VERSION, "flash_bytes": FLASH_BYTES,
        "merged_flash_offset": 0, "factory_boot_stack_report": stack_report,
        "limitations": ["Host checks do not establish target stability, RF, EMC, electrical or thermal compliance.",
                        "Test firmware contains development-only flags and is not the production image.",
                        "Current source hashes identify inputs; this package is not build attestation.",
                        "merged_firmware.bin is for fresh installation: its erased gaps cover NVS/OTA metadata and can erase credentials/configuration. Existing devices require the approved migrate_flash_v2.py procedure or application-only OTA."],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish only a complete candidate; exceptions cannot leave a success manifest.
    with tempfile.TemporaryDirectory(dir=output.parent, prefix=".candidate-") as tmp:
        staging = Path(tmp) / "candidate"
        staging.mkdir()
        for name, path in artifacts.items():
            destination = staging / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
            if fingerprint(destination) != records[name]:
                raise ValueError(f"Build artifact changed during packaging: {name}")
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        staging.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--node-root", type=Path)
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--platformio-core-dir", type=Path,
                        default=Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio")))
    parser.add_argument("--merged-firmware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo_root.resolve()
    node = (args.node_root or repo / "LumigridLedNode").resolve()
    try:
        manifest = package_candidate(repo, node, (args.build_root or node / ".pio/build").resolve(),
                                     args.platformio_core_dir.resolve(), args.merged_firmware.resolve(),
                                     args.output.resolve())
    except (ValueError, OSError, KeyError, PackageNotFoundError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Candidate packaging failed: {error}") from error
    print(f"Candidate manifest: {args.output / 'manifest.json'}")
    print(f"Source {manifest['source']['commit']}; dirty={manifest['source']['dirty']}; hardware_validation=NOT_RUN")


if __name__ == "__main__":
    main()
