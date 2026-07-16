from pathlib import Path


def generate_gamma_lut(output_path: Path, gamma: float = 2.2) -> None:
    values = [round((index / 255.0) ** gamma * 255.0) for index in range(256)]
    rows = [", ".join(str(value) for value in values[i:i + 16]) for i in range(0, 256, 16)]
    body = "\n    ".join(rows)
    text = (
        "#pragma once\n#include <array>\n#include <cstdint>\n\n"
        "namespace lg {\n"
        "constexpr std::array<uint8_t, 256> kGamma22 = {\n"
        f"    {body}\n"
        "};\n} // namespace lg\n"
    )
    output_path.write_text(text)


if __name__ == "__main__":
    path = Path(__file__).resolve().parents[1] / "include" / "frame" / "lg_gamma_lut.hpp"
    generate_gamma_lut(path)
    print(f"Gamma LUT written to {path}")
