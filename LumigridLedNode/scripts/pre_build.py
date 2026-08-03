Import("env")
import os

input_file = env.subst("$PROJECT_DIR/main/dashboard.html")
output_file = env.subst("$PROJECT_DIR/components/factory/include/dashboard.hpp")

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
