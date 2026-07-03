import json
import re
import os

# Load logos
with open('logos_b64.json', 'r') as f:
    logos = json.load(f)

# Load index.htm
with open(r'wled00\data\index.htm', 'r', encoding='utf-8') as f:
    html = f.read()

# Replace favicon
html = re.sub(
    r'<link rel="shortcut icon" href=".*?"/>',
    f'<link rel="shortcut icon" href="{logos["brand_favicon"]}"/>',
    html
)

# Replace apple-touch-icon
html = re.sub(
    r'<link rel="apple-touch-icon" href=".*?" sizes="180x180"/>',
    f'<link rel="apple-touch-icon" href="{logos["brand_app_icon"]}" sizes="180x180"/>',
    html
)

# Replace WLED logo in modal
html = re.sub(
    r'<img class="wi" alt="" src=".*?" />',
    f'<img class="wi" style="filter: invert(1); max-width: 150px; height: auto; margin: 10px auto; display: block;" alt="" src="{logos["controller_logo"]}" />',
    html
)

# Replace WLED mentions
html = html.replace('WLED', 'Lumigrid LED Node')
html = html.replace('wled1234', 'lumigrid12')

# Add custom glassmorphism styling
custom_css = """
<style>
/* Glassmorphism Reclame Fabriek Theme */
body {
    background-color: #0d0000;
    color: #f2f2f2;
}
#bg {
    background: radial-gradient(circle at center, #3a0000 0%, #0d0000 100%);
}
.tabcontent, .modal, .tab.bot {
    background: rgba(40, 0, 0, 0.6) !important;
    backdrop-filter: blur(12px) !important;
    -webkit-backdrop-filter: blur(12px) !important;
    border: 1px solid rgba(255, 51, 51, 0.2);
    box-shadow: 0 4px 30px rgba(0, 0, 0, 0.5);
    border-radius: 12px;
}
button, .btn, .sliderwrap input[type=range]::-webkit-slider-thumb {
    background-color: rgba(255, 51, 51, 0.8) !important;
    border-color: #ff3333 !important;
    color: white !important;
}
button:hover, .btn:hover {
    background-color: #ff3333 !important;
    box-shadow: 0 0 10px rgba(255, 51, 51, 0.6);
}
.sliderdisplay {
    background-color: rgba(255, 255, 255, 0.1);
    border: 1px solid rgba(255, 51, 51, 0.3);
}
</style>
"""

html = html.replace('<link rel="stylesheet" href="index.css">', '<link rel="stylesheet" href="index.css">\n' + custom_css)

with open(r'wled00\data\index.htm', 'w', encoding='utf-8') as f:
    f.write(html)

print('Updated index.htm')
