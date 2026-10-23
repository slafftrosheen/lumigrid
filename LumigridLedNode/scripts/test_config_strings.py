"""Exercise ConfigStore's production bounded copy with ASan and UBSan."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    source = (root / "components/config/ConfigStore.cpp").read_text()
    helper = source[source.index("void safe_strncpy("):source.index("void make_key(")]
    cxx = shutil.which(os.environ.get("CXX", "g++"))
    if not cxx:
        raise SystemExit("A host C++ compiler is required")
    harness = r'''
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdio>
''' + helper + r'''
int main() {
    struct Guarded { char before = 'L'; char text[128]; char after = 'R'; } output;
    std::memset(output.text, 0x5a, sizeof(output.text));
    safe_strncpy(output.text, "Failed to load device config from NVS", sizeof(output.text));
    assert(std::strcmp(output.text, "Failed to load device config from NVS") == 0);
    for (size_t i = std::strlen(output.text); i < sizeof(output.text); ++i)
        assert(output.text[i] == '\0');
    assert(output.before == 'L' && output.after == 'R');

    char* tiny = new char[2]{'A', '\0'};
    safe_strncpy(output.text, tiny, sizeof(output.text));
    assert(std::strcmp(output.text, "A") == 0);
    delete[] tiny;

    const char legacy[4] = {'A', 'B', 'C', 'D'}; // Unterminated persisted field.
    char bounded[4] = {};
    safe_strncpy(bounded, legacy, sizeof(bounded));
    assert(std::memcmp(bounded, "ABC\0", sizeof(bounded)) == 0);
    safe_strncpy(bounded, "", sizeof(bounded));
    for (char c : bounded) assert(c == '\0');
    char single = 'x';
    safe_strncpy(&single, "long source", 1);
    assert(single == '\0');
    safe_strncpy(nullptr, nullptr, 0);
    std::puts("ConfigStore bounded copy: 6 cases passed (ASan/UBSan)");
}
'''
    with tempfile.TemporaryDirectory(prefix="lumigrid-config-strings-") as directory:
        temp = Path(directory)
        cpp = temp / "test.cpp"
        executable = temp / "test"
        cpp.write_text(harness)
        subprocess.run([cxx, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-fno-pie", "-no-pie", str(cpp), "-o", str(executable)], check=True)
        # Restricted runners can deny LeakSanitizer's /proc thread scan. This
        # harness checks bounds/UB, and explicitly releases its one allocation.
        env = dict(os.environ)
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        subprocess.run([str(executable)], check=True, env=env)


if __name__ == "__main__":
    main()
