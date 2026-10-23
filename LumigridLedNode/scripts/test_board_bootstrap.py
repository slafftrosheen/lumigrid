"""Run the real boot GPIO sequence against an ESP32 input/output latch model."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile


def main():
    node = Path(__file__).resolve().parents[1]
    compiler = shutil.which(os.environ.get("CXX", "g++"))
    if not compiler:
        raise SystemExit("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="lumigrid-boot-gpio-") as directory:
        temp = Path(directory)
        (temp / "driver").mkdir()
        (temp / "esp_err.h").write_text(
            "#pragma once\nusing esp_err_t=int; constexpr int ESP_OK=0, ESP_FAIL=-1;\n")
        (temp / "driver/gpio.h").write_text(
            "#pragma once\n#include <cstdint>\n#include <esp_err.h>\n"
            "enum gpio_num_t { " + ", ".join(f"GPIO_NUM_{n}={n}" for n in range(40)) + " };\n"
            "enum gpio_mode_t { GPIO_MODE_DISABLE=0, GPIO_MODE_INPUT=1, GPIO_MODE_OUTPUT=2, GPIO_MODE_INPUT_OUTPUT=3 };\n"
            "esp_err_t gpio_reset_pin(gpio_num_t);\n"
            "esp_err_t gpio_set_level(gpio_num_t, uint32_t);\n"
            "esp_err_t gpio_set_direction(gpio_num_t, gpio_mode_t);\n"
            "int gpio_get_level(gpio_num_t);\n")
        harness = temp / "test.cpp"
        harness.write_text(r'''
#include <array>
#include <cassert>
#include <cstdio>
#include "BoardOutputSafety.hpp"
#include "lg_board_pins.hpp"
struct Pad { int latch = 0; int mode = 0; bool touched = false; };
std::array<Pad, 40> pads;
int fail_at = -1, calls = 0, oe_low_pulses = 0, forced_oe = -1;
bool fail() { return calls++ == fail_at; }
void observe(int pin) {
    if (pin == lg::board::kPcaOe && (pads[pin].mode & GPIO_MODE_OUTPUT) && pads[pin].latch == 0)
        ++oe_low_pulses;
}
int gpio_reset_pin(gpio_num_t pin) {
    pads[pin].touched = true;
    if (fail()) return ESP_FAIL;
    pads[pin].mode = 0; // reset leaves latch contents intact, output disconnected
    return ESP_OK;
}
int gpio_set_level(gpio_num_t pin, uint32_t level) {
    if (fail()) return ESP_FAIL;
    pads[pin].latch = level;
    observe(pin);
    return ESP_OK;
}
int gpio_set_direction(gpio_num_t pin, gpio_mode_t mode) {
    if (fail()) return ESP_FAIL;
    pads[pin].mode = mode;
    observe(pin);
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) {
    if (!(pads[pin].mode & GPIO_MODE_INPUT)) return 0; // documented ESP-IDF semantics
    if (pin == lg::board::kPcaOe && forced_oe >= 0) return forced_oe;
    return pads[pin].latch;
}
void reset() { pads = {}; calls = 0; fail_at = -1; oe_low_pulses = 0; forced_oe = -1; }
int main() {
    using namespace lg::board;
    reset();
    assert(force_outputs_safe_early() == ESP_OK);
    assert(oe_low_pulses == 0); // no low pulse when the output latch initially contains zero
    auto diag = get_boot_output_diagnostics();
    assert(diag.early_safety_applied && diag.pca_oe_high_at_bootstrap && diag.aled_low_mask == 0xff);
    assert(sample_pca_oe_level() == 1);
    for (auto pin : kAledPins) assert(pads[pin].latch == 0);

    // Every GPIO operation can fail; none may report successful bootstrap.
    for (int operation = 0; operation < 27; ++operation) {
        reset(); fail_at = operation;
        assert(force_outputs_safe_early() == ESP_FAIL);
        assert(!get_boot_output_diagnostics().early_safety_applied);
        for (auto pin : kAledPins) assert(pads[pin].touched); // remaining outputs still made safe
    }
    reset(); forced_oe = 0;
    assert(force_outputs_safe_early() == ESP_FAIL);
    assert(!get_boot_output_diagnostics().pca_oe_high_at_bootstrap);
    assert(!get_boot_output_diagnostics().early_safety_applied);
    std::puts("Boot GPIO sequence: safe latch, readable pads, 27 failures, low /OE rejection passed");
}
''')
        executable = temp / "test"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(temp), "-I", str(node / "components/board/include"),
                        str(harness), str(node / "components/board/BoardOutputSafety.cpp"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
