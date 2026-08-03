import os
import sys

def main():
    if len(sys.argv) != 3:
        print("Usage: build_ui.py <input.html> <output.hpp>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    print(f"Building UI from {input_file} to {output_file}")

    with open(input_file, 'r', encoding='utf-8') as f:
        html = f.read()

    hpp_content = f"""#pragma once

static const char dashboard_html[] = R"LGHTML({html})LGHTML";
"""

    os.makedirs(os.path.dirname(output_file), exist_ok=True)

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(hpp_content)

    print("Dashboard header generated successfully.")

if __name__ == "__main__":
    main()
