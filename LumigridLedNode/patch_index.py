"""
patch_index.py — LumiGrid branding patch for WLED index.htm
Applies brand identity, replaces WLED strings, and injects an inline
CSS block that aligns the main control page with the lander design system.
"""

import json
import re
import os

# ── Load assets ────────────────────────────────────────────────────────────────
with open('logos_b64.json', 'r') as f:
    logos = json.load(f)

with open(r'wled00\data\index.htm', 'r', encoding='utf-8') as f:
    html = f.read()

# ── Identity patches ───────────────────────────────────────────────────────────

# Favicon
html = re.sub(
    r'<link rel="shortcut icon" href=".*?"/>',
    f'<link rel="shortcut icon" href="{logos["brand_favicon"]}"/>',
    html
)

# Apple touch icon
html = re.sub(
    r'<link rel="apple-touch-icon" href=".*?" sizes="180x180"/>',
    f'<link rel="apple-touch-icon" href="{logos["brand_app_icon"]}" sizes="180x180"/>',
    html
)

# Logo in info modal
html = re.sub(
    r'<img class="wi" alt="" src=".*?" />',
    f'<img class="wi" style="filter:invert(1);max-width:140px;height:auto;margin:12px auto;display:block;" alt="LumiGrid" src="{logos["controller_logo"]}" />',
    html
)

# Text replacements
html = html.replace('WLED', 'LumiGrid Node')
html = html.replace('wled1234', 'lumigrid12')

# ── Inline CSS (lander-synced theme for index.htm UI elements) ─────────────────
#
# WLED's index.htm uses a different CSS file (index.css, not style.css) and
# inline styles from JS. This block patches the rendered page so that:
#   • Background matches lander --bg (#050505) + brand radial halos
#   • Tabs / modals / cards → glassmorphism matching lander .card
#   • Buttons → lander --rf-magenta (#ff2d55) with glow
#   • Sliders, inputs, selects → lander glass-input style
#   • Text → lander --ink (#f5f5f7) / --muted (#a1a1a6)
#   • Typography → SF Pro / Inter (same as lander)
#   • Segment/preset chips → lander .chip style

custom_css = """
<style id="lumigrid-theme">
/* ── LumiGrid Lander-synced tokens ───────────────────────── */
:root {
  --lg-bg:           #050505;
  --lg-ink:          #f5f5f7;
  --lg-muted:        #a1a1a6;
  --lg-magenta:      #ff2d55;
  --lg-cherry:       #ff3b30;
  --lg-cyan:         #00d4ff;
  --lg-amber:        #ff9500;
  --lg-glass-bg:     rgba(29,29,31,.78);
  --lg-glass-hi:     rgba(29,29,31,.92);
  --lg-glass-border: rgba(255,255,255,.10);
  --lg-glass-accent: rgba(255,45,85,.22);
  --lg-blur:         blur(20px);
  --lg-shadow:       0 8px 32px rgba(0,0,0,.55);
  --lg-font:         -apple-system,BlinkMacSystemFont,"SF Pro Text",Inter,system-ui,sans-serif;
  --lg-btn-bg:       rgba(255,45,85,.88);
  --lg-btn-hover:    #ff3b30;
}

/* Global background — matches lander body::before */
body {
  font-family: var(--lg-font) !important;
  background:
    radial-gradient(ellipse at 20% 0%,   rgba(255,45,85,.10) 0%, transparent 50%),
    radial-gradient(ellipse at 80% 100%, rgba(0,212,255,.07) 0%, transparent 50%),
    #050505 !important;
  background-attachment: fixed !important;
  color: var(--lg-ink) !important;
  -webkit-font-smoothing: antialiased;
}

/* Glass surface helper — applied to all major panels */
.tabcontent,
.modal,
.tab.bot,
#info,
#nodes,
#sliders,
#fxlist,
#segcont,
.seg,
.slist,
.plist,
#topmenu,
#bot {
  background: var(--lg-glass-bg) !important;
  backdrop-filter: var(--lg-blur) !important;
  -webkit-backdrop-filter: var(--lg-blur) !important;
  border: 1px solid var(--lg-glass-border) !important;
  box-shadow: var(--lg-shadow) !important;
  color: var(--lg-ink) !important;
}

/* Card border-radius variants */
.tabcontent, .modal { border-radius: 20px !important; }
.tab.bot            { border-radius: 16px !important; }
.seg, .slist > li   { border-radius: 14px !important; }

/* Buttons */
button,
.btn,
.cbtn {
  font-family: var(--lg-font) !important;
  font-weight: 600 !important;
  background: var(--lg-btn-bg) !important;
  color: #ffffff !important;
  border: 1px solid rgba(255,255,255,.15) !important;
  border-radius: 999px !important;
  box-shadow: 0 4px 16px rgba(255,45,85,.38) !important;
  transition: background .25s ease, box-shadow .25s ease, transform .2s ease !important;
}
button:hover, .btn:hover, .cbtn:hover {
  background: var(--lg-btn-hover) !important;
  box-shadow: 0 8px 28px rgba(255,45,85,.55) !important;
  transform: translateY(-1px) !important;
}
button:active, .btn:active { transform: translateY(0) !important; }

/* Active / selected state — cyan accent (matches lander selected chip) */
button.selected,
.btn.active,
.seg.selected {
  border-color: var(--lg-cyan) !important;
  box-shadow: 0 0 12px rgba(0,212,255,.35) !important;
}

/* Power / on-state button — use cyan to contrast with magenta */
#buttonPower.on {
  background: rgba(0,212,255,.85) !important;
  box-shadow: 0 4px 16px rgba(0,212,255,.40) !important;
}

/* Sliders */
.sliderwrap input[type=range]::-webkit-slider-thumb,
.sliderwrap input[type=range]::-moz-range-thumb {
  background: var(--lg-magenta) !important;
  border: 2px solid rgba(255,255,255,.3) !important;
  box-shadow: 0 0 8px rgba(255,45,85,.5) !important;
}
.sliderwrap input[type=range]::-webkit-slider-runnable-track {
  background: rgba(255,255,255,.08) !important;
  border-radius: 999px !important;
}
.sliderdisplay {
  background: rgba(255,255,255,.06) !important;
  border: 1px solid rgba(255,255,255,.12) !important;
  color: var(--lg-ink) !important;
  border-radius: 8px !important;
}

/* Text / headings */
h2, h3, h4, h5 {
  color: var(--lg-ink) !important;
  font-family: var(--lg-font) !important;
}
label, p, small, td, th {
  color: var(--lg-ink) !important;
  font-family: var(--lg-font) !important;
}
.muted, .dimmed { color: var(--lg-muted) !important; }

/* Inputs and selects — lander glass-input */
input[type=text], input[type=number], input[type=password], select {
  background: rgba(255,255,255,.06) !important;
  color: var(--lg-ink) !important;
  border: 1px solid rgba(255,255,255,.14) !important;
  border-radius: 8px !important;
  font-family: var(--lg-font) !important;
}
input:focus, select:focus {
  outline: none !important;
  border-color: var(--lg-magenta) !important;
  box-shadow: 0 0 0 2px rgba(255,45,85,.25) !important;
}
input[type=checkbox] { accent-color: var(--lg-magenta) !important; }

/* Links */
a { color: var(--lg-cyan) !important; }
a:hover { color: #fff !important; }

/* Preset / segment chips */
.preset, .plabel, .slabel {
  background: rgba(255,255,255,.06) !important;
  border: 1px solid rgba(255,255,255,.12) !important;
  border-radius: 999px !important;
  color: var(--lg-ink) !important;
  transition: background .2s ease, border-color .2s ease !important;
}
.preset:hover, .plabel:hover {
  background: rgba(255,255,255,.12) !important;
  border-color: rgba(255,255,255,.24) !important;
}

/* Effect list items */
#fxlist li, #pallist li {
  background: rgba(255,255,255,.04) !important;
  border-radius: 8px !important;
  color: var(--lg-ink) !important;
  transition: background .2s ease !important;
}
#fxlist li:hover, #pallist li:hover,
#fxlist li.selected, #pallist li.selected {
  background: rgba(255,45,85,.18) !important;
  border-color: rgba(255,45,85,.30) !important;
}

/* Top navigation bar */
#topmenu {
  background: rgba(5,5,5,.92) !important;
  border-bottom: 1px solid rgba(255,255,255,.08) !important;
  backdrop-filter: blur(16px) !important;
  -webkit-backdrop-filter: blur(16px) !important;
  border-radius: 0 !important;
  box-shadow: 0 1px 16px rgba(0,0,0,.5) !important;
}

/* Toast */
#toast {
  background: var(--lg-glass-bg) !important;
  border: 1px solid var(--lg-glass-border) !important;
  border-radius: 12px !important;
  box-shadow: var(--lg-shadow) !important;
  color: var(--lg-ink) !important;
  backdrop-filter: var(--lg-blur) !important;
}
#toast.show  { border-color: rgba(52,199,89,.4) !important; background: rgba(10,30,15,.88) !important; }
#toast.error { border-color: var(--lg-glass-accent) !important; background: rgba(40,5,5,.88) !important; }

/* Color picker ring highlight */
.irowc .colorButton.active {
  box-shadow: 0 0 0 3px var(--lg-magenta), 0 0 12px rgba(255,45,85,.5) !important;
}

/* Segment active color indicator strip */
.seg .segc { background: var(--lg-magenta) !important; }

/* Scrollbar (webkit) */
::-webkit-scrollbar { width: 6px; height: 6px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: rgba(255,255,255,.14); border-radius: 999px; }
::-webkit-scrollbar-thumb:hover { background: rgba(255,45,85,.45); }
</style>
"""

html = html.replace(
    '<link rel="stylesheet" href="index.css">',
    '<link rel="stylesheet" href="index.css">\n' + custom_css
)

# ── Write output ───────────────────────────────────────────────────────────────
with open(r'wled00\data\index.htm', 'w', encoding='utf-8') as f:
    f.write(html)

print('✓ index.htm patched with LumiGrid lander-synced theme')
