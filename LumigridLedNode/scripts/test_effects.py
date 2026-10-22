#!/usr/bin/env python3
"""Compile and exercise the real scene renderer without an ESP-IDF installation.

Usage: python3 scripts/test_effects.py [--no-sanitize] [--case CASE]
Only the GPIO enum needed by the real board pin header is supplied by the host
harness. Effect, palette, frame, and configuration code comes from production.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-sanitize", action="store_true", help="omit ASan/UBSan")
    parser.add_argument("--case", action="append", default=[], help="run a named test group")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    compiler = os.environ.get("CXX", "g++")
    if not shutil.which(compiler):
        parser.error(f"C++ compiler not found: {compiler}")

    with tempfile.TemporaryDirectory(prefix="lumigrid-effects-") as temp_dir:
        temp = Path(temp_dir)
        (temp / "driver").mkdir()
        (temp / "driver" / "gpio.h").write_text(
            "#pragma once\n#include <cstdint>\n"
            "enum gpio_num_t { "
            + ", ".join(f"GPIO_NUM_{pin} = {pin}" for pin in range(40))
            + " };\n",
            encoding="utf-8",
        )
        executable = temp / "effects_test"
        command = [compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-pedantic"]
        if not args.no_sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        for directory in (temp, root / "components/config/include", root / "components/board/include", root / "components/signage/include"):
            command += ["-I", str(directory)]
        command += [str(root / "test/effects_test.cpp"), str(root / "components/signage/EffectRenderer.cpp"), "-o", str(executable)]
        subprocess.run(command, check=True)
        env = os.environ.copy()
        # LSan requires /proc access unavailable in some container sandboxes.
        # Address/undefined-behavior checks remain enabled; renderer owns no heap.
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
        env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
        return subprocess.run([str(executable), *args.case], env=env, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
