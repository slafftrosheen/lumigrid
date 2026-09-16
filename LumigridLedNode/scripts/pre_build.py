Import("env")
import os
import gzip

input_file = env.subst("$PROJECT_DIR/main/dashboard.html")
output_file = env.subst("$PROJECT_DIR/components/factory/include/dashboard.hpp")

print(f"Building UI from {input_file} to {output_file}")

with open(input_file, 'rb') as f:
    html_bytes = f.read()

compressed = gzip.compress(html_bytes, compresslevel=9)

bytes_per_line = 16
hex_lines = []
for i in range(0, len(compressed), bytes_per_line):
    chunk = compressed[i:i+bytes_per_line]
    hex_lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

hex_content = "\n".join(hex_lines)

hpp_content = f"""#pragma once
#include <cstddef>
#include <cstdint>

static const uint8_t dashboard_html_gz[] = {{
{hex_content}
}};

static const size_t dashboard_html_gz_len = {len(compressed)};
"""

os.makedirs(os.path.dirname(output_file), exist_ok=True)

with open(output_file, 'w', encoding='utf-8') as f:
    f.write(hpp_content)

print(f"Dashboard header generated successfully: {len(html_bytes)} -> {len(compressed)} bytes gzipped.")
