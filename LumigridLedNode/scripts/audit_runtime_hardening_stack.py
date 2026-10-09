from __future__ import annotations
import argparse
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path

FIXTURE_TYPES = {
    "OutputController", "LightingRuntime", "DmxRouter", "RenderFrame",
    "LiveDmxFrame", "DeviceConfig", "DeviceConfigV1", "DeviceConfigV2",
    "DeviceConfigV3", "SceneConfig", "SceneMotionConfig", "Sequence",
    "CueList", "CalendarSchedule", "LumigridRuntime", "RuntimeSnapshot",
    "CueSyncSnapshot", "CertificationProbeSnapshot", "RuntimeMemorySnapshot",
}
TEST_DEF = re.compile(r"^\s*bool\s+(test_[A-Za-z0-9_]+)\s*\(")
HEAP_FIXTURE = re.compile(r"LG_TEST_HEAP\(\s*([A-Za-z0-9_:]+)\s*,")
HEAP_BUFFER = re.compile(r"LG_TEST_BUFFER\(\s*([A-Za-z0-9_]+)\s*,\s*(\d+)\s*\)")
SU_ROW = re.compile(r"^(.*?):(\d+):\d+:.*?::(test_[A-Za-z0-9_]+)\(.*?\)\s+(\d+)\s+(?:static|dynamic.*)$")

SIZE_FIELDS = [
    "OutputController", "LightingRuntime", "DmxRouter", "RenderFrame",
    "LiveDmxFrame", "DeviceConfig", "DeviceConfigV1", "DeviceConfigV2",
    "DeviceConfigV3", "SceneConfig", "SceneMotionConfig", "Sequence",
    "CueList", "CalendarSchedule", "LumigridRuntime", "RuntimeSnapshot",
]

def fixture_sizes(elf: Path) -> dict[str, int]:
    package_root = Path.home() / ".platformio" / "packages" / "toolchain-xtensa-esp-elf" / "bin"
    nm = shutil.which("xtensa-esp-elf-nm") or str(package_root / "xtensa-esp-elf-nm.exe")
    objdump = shutil.which("xtensa-esp-elf-objdump") or str(package_root / "xtensa-esp-elf-objdump.exe")
    if not Path(nm).exists() or not Path(objdump).exists() or not elf.exists():
        return {}
    symbols = subprocess.run([nm, "-S", "-C", str(elf)], capture_output=True, text=True, check=True).stdout
    marker = re.search(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+lg_runtime_hardening_fixture_sizes$", symbols, re.M)
    if not marker:
        return {}
    start = int(marker.group(1), 16)
    size = int(marker.group(2), 16)
    dump = subprocess.run([objdump, "-s", "-j", ".flash.rodata", str(elf)], capture_output=True, text=True, check=True).stdout
    bytes_by_address: dict[int, int] = {}
    for line in dump.splitlines():
        parts = line.split()
        if not parts or not re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            continue
        line_start = int(parts[0], 16)
        groups = [part for part in parts[1:] if re.fullmatch(r"[0-9a-fA-F]{8}", part)]
        line_data = b"".join(bytes.fromhex(group) for group in groups)
        for index, value in enumerate(line_data):
            bytes_by_address[line_start + index] = value
    data = bytes(bytes_by_address[address] for address in range(start, start + size))
    values = struct.unpack_from("<" + "I" * len(SIZE_FIELDS), data)
    return dict(zip(SIZE_FIELDS, values))

def main() -> int:
    parser = argparse.ArgumentParser(description="Build a source/.su audit of runtime hardening fixture stack use")
    parser.add_argument("--build-dir", type=Path, required=True, help="PlatformIO build root containing test/main/*.su and test/firmware.elf")
    parser.add_argument("--source", type=Path, default=Path("LumigridLedNode/main/RuntimeHardeningTests.cpp"))
    parser.add_argument("--output", type=Path, help="Write Markdown report; defaults to stdout")
    args = parser.parse_args()

    su_files = list(args.build_dir.rglob("RuntimeHardeningTests.cpp.su"))
    if not su_files:
        parser.error("RuntimeHardeningTests.cpp.su was not found under --build-dir; build env:test with -fstack-usage first")

    function_at_line: list[tuple[int, str]] = []
    fixture_types: dict[str, set[str]] = {}
    buffers: dict[str, set[str]] = {}
    for number, line in enumerate(args.source.read_text(encoding="utf-8").splitlines(), 1):
        definition = TEST_DEF.match(line)
        if definition:
            function_at_line.append((number, definition.group(1)))
        current_function = next((name for start, name in reversed(function_at_line) if start <= number), None)
        if current_function is None:
            continue
        for match in HEAP_FIXTURE.finditer(line):
            fixture_types.setdefault(current_function, set()).add(match.group(1))
        for match in HEAP_BUFFER.finditer(line):
            buffers.setdefault(current_function, set()).add(f"char[{match.group(2)}] ({match.group(1)})")

    stacks: dict[str, int] = {}
    for su in su_files:
        for line in su.read_text(encoding="utf-8", errors="replace").splitlines():
            if "::test_" not in line:
                continue
            match = re.search(r"::(test_[A-Za-z0-9_]+)\([^\t]*\)\s+(\d+)\s+(?:static|dynamic.*)$", line)
            if match:
                stacks[match.group(1)] = max(stacks.get(match.group(1), 0), int(match.group(2)))

    names = sorted(set(stacks) | set(fixture_types) | set(buffers))
    sizes = fixture_sizes(args.build_dir / "firmware.elf")
    rows = ["# Runtime Hardening Stack Audit", "", f"Compiler stack usage source: `{su_files[0]}` (`-fstack-usage`, env:test).", "",
            "Large fixtures listed below are `LG_TEST_HEAP` RAII-owned allocations. Test buffers use checked heap allocation and are included in peak fixture pressure.", "",
            "## Fixture sizes from the diagnostic ELF", "", "| Object type | `sizeof` (bytes) |", "|---|---:|"]
    if sizes:
        rows.extend(f"| `{name}` | {size} |" for name, size in sizes.items())
    else:
        rows.append("| (size marker unavailable) | (not extracted) |")
    rows += ["", "## Test function source/build audit", "",
            "| Test function | Local large fixture type(s) | Estimated function stack (bytes) | Allocation ownership |",
            "|---|---|---:|---|"]
    for name in names:
        types = sorted(fixture_types.get(name, set()))
        types.extend(sorted(buffers.get(name, set())))
        stack = stacks.get(name)
        ownership = "checked heap + RAII" if types else "small locals / compiler frame"
        type_text = ", ".join("`" + item + "`" for item in types) if types else "(none)"
        stack_text = str(stack) if stack is not None else "missing .su record"
        rows.append(f"| `{name}` | {type_text} | {stack_text} | {ownership} |")
    rows += ["", "`-fstack-usage` reports each compiled function's static frame; it does not include callees, interrupt use, or runtime library stack use.", ""]
    report = "\n".join(rows)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8", newline="\n")
    else:
        print(report, end="")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
