#pragma once

static const char dashboard_html[] = R"LGHTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
    <title>Lumigrid OS</title>
    <style>
        /* ═══════════════════════════════════════════════════════
           LUMIGRID OS — DESIGN SYSTEM
           ═══════════════════════════════════════════════════════ */
        :root {
            --bg: #05060f;
            --bg-subtle: #0a0b18;
            --surface: rgba(14, 15, 28, 0.78);
            --surface-elevated: rgba(22, 24, 42, 0.88);
            --surface-hover: rgba(32, 35, 55, 0.92);
            --surface-active: rgba(42, 46, 68, 0.95);
            --border: rgba(255, 255, 255, 0.07);
            --border-subtle: rgba(255, 255, 255, 0.04);
            --border-focus: rgba(129, 140, 248, 0.5);
            --text: #eef0f6;
            --text-secondary: #a0a4b8;
            --text-muted: #6b7084;
            --primary: #6366f1;
            --primary-dim: #4f46e5;
            --primary-soft: rgba(99, 102, 241, 0.14);
            --primary-glow: rgba(99, 102, 241, 0.35);
            --accent: #34d399;
            --accent-soft: rgba(52, 211, 153, 0.14);
            --accent-glow: rgba(52, 211, 153, 0.3);
            --danger: #f87171;
            --danger-bg: rgba(127, 29, 29, 0.35);
            --danger-glow: rgba(248, 113, 113, 0.3);
            --warning: #fbbf24;
            --radius-sm: 8px;
            --radius: 12px;
            --radius-lg: 16px;
            --radius-xl: 20px;
            --radius-full: 999px;
            --font: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', system-ui, sans-serif;
            --ease: cubic-bezier(0.4, 0, 0.2, 1);
            --transition: 0.2s cubic-bezier(0.4, 0, 0.2, 1);
            --transition-slow: 0.35s cubic-bezier(0.4, 0, 0.2, 1);
        }

        /* ── Reset ── */
        *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: var(--font);
            background: var(--bg);
            color: var(--text);
            line-height: 1.55;
            min-height: 100vh;
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
            overflow-x: hidden;
        }
        body::after {
            content: '';
            position: fixed; inset: 0;
            background:
                radial-gradient(ellipse 80% 50% at 10% 45%, rgba(99,102,241,.07), transparent),
                radial-gradient(ellipse 60% 70% at 90% 25%, rgba(52,211,153,.05), transparent);
            pointer-events: none; z-index: -1;
        }

        /* ── Typography ── */
        h1, h2, h3, h4, h5 { font-weight: 650; letter-spacing: -0.02em; line-height: 1.25; }
        h3 { font-size: 1.3rem; }
        h4 { font-size: 0.95rem; margin-bottom: 0.6rem; }
        .eyebrow {
            color: #a5b4fc; font-size: 0.68rem; font-weight: 800;
            letter-spacing: 0.15em; text-transform: uppercase;
        }
        .muted { color: var(--text-muted); font-size: 0.88rem; }
        .help { color: var(--text-muted); font-size: 0.75rem; line-height: 1.5; }
        .mb-1 { margin-bottom: 1rem; }

        /* ══════════════════════════════════════════════════════
           LOGIN SCREEN
           ══════════════════════════════════════════════════════ */
        #experience-auth, #product-app { min-height: 100vh; }
        #experience-auth[hidden], #product-app[hidden] { display: none; }
        #experience-auth {
            display: grid; place-items: center; padding: 1.5rem;
            position: relative; overflow: hidden;
        }
        #experience-auth::before {
            content: '';
            position: absolute;
            width: 250%; height: 250%; top: -75%; left: -75%;
            background:
                radial-gradient(circle at 25% 35%, rgba(99,102,241,.22), transparent 35%),
                radial-gradient(circle at 70% 65%, rgba(52,211,153,.16), transparent 30%),
                radial-gradient(circle at 45% 85%, rgba(129,140,248,.1), transparent 25%);
            animation: aurora 25s ease-in-out infinite alternate;
            pointer-events: none;
        }
        @keyframes aurora {
            0%   { transform: translate(0, 0) rotate(0deg) scale(1); }
            33%  { transform: translate(-4%, 3%) rotate(2deg) scale(1.03); }
            66%  { transform: translate(3%, -2%) rotate(-1.5deg) scale(0.97); }
            100% { transform: translate(-2%, 1%) rotate(0.8deg) scale(1.01); }
        }
        .experience-login {
            position: relative;
            width: min(26rem, 100%); padding: 2.5rem 2rem 2rem;
            border: 1px solid rgba(255,255,255,0.08);
            border-radius: var(--radius-xl);
            background: rgba(10, 12, 24, 0.9);
            backdrop-filter: blur(24px);
            box-shadow:
                0 2rem 5rem rgba(0,0,0,.5),
                0 0 0 0.5px rgba(255,255,255,0.06) inset,
                0 1px 0 rgba(255,255,255,0.04) inset;
        }
        .login-logo {
            display: flex; align-items: center; gap: 0.75rem;
            margin-bottom: 1.5rem;
        }
        .login-logo svg { flex-shrink: 0; filter: drop-shadow(0 0 8px rgba(99,102,241,0.4)); }
        .experience-login h1 {
            font-size: clamp(1.8rem, 7vw, 2.6rem);
            letter-spacing: -0.05em;
            margin: 0.25rem 0 0.5rem;
            background: linear-gradient(135deg, var(--text) 0%, #a5b4fc 50%, var(--accent) 100%);
            background-size: 200% 200%;
            -webkit-background-clip: text; background-clip: text;
            -webkit-text-fill-color: transparent;
            animation: shimmer 6s ease infinite;
        }
        @keyframes shimmer {
            0%, 100% { background-position: 0% 50%; }
            50% { background-position: 100% 50%; }
        }
        .experience-login label, .field label {
            display: block; font-size: 0.78rem; font-weight: 700;
            letter-spacing: 0.04em; margin: 1.4rem 0 0.45rem;
            color: var(--text-secondary);
        }

        /* ══════════════════════════════════════════════════════
           FORM CONTROLS
           ══════════════════════════════════════════════════════ */
        input, select, button { font-family: inherit; }
        input, select {
            font-size: 0.9rem; border-radius: var(--radius-sm);
            border: 1px solid var(--border);
            background: rgba(5, 6, 15, 0.6);
            color: var(--text); padding: 0.7rem 0.85rem;
            width: 100%;
            transition: border-color var(--transition), box-shadow var(--transition), background var(--transition);
        }
        input:focus, select:focus {
            outline: none; border-color: var(--primary);
            box-shadow: 0 0 0 3px var(--primary-soft), 0 0 16px rgba(99,102,241,0.12);
            background: rgba(8, 10, 22, 0.8);
        }
        input::placeholder { color: var(--text-muted); }
        select { cursor: pointer; }

        /* Toggle Switch — targets checkboxes inside .switch-row */
        .switch-row { display: flex; align-items: center; justify-content: space-between; gap: 0.8rem; padding: 0.55rem 0; font-size: 0.82rem; }
        .switch-row label { margin: 0; }
        .switch-row input[type="checkbox"] {
            -webkit-appearance: none; appearance: none;
            width: 2.75rem; height: 1.5rem;
            border-radius: var(--radius-full);
            background: rgba(255,255,255,0.08);
            border: 1px solid rgba(255,255,255,0.12);
            cursor: pointer; position: relative;
            transition: background var(--transition), border-color var(--transition), box-shadow var(--transition);
            flex-shrink: 0;
        }
        .switch-row input[type="checkbox"]::after {
            content: '';
            position: absolute; top: 2px; left: 2px;
            width: calc(1.5rem - 6px); height: calc(1.5rem - 6px);
            border-radius: 50%; background: rgba(255,255,255,0.7);
            box-shadow: 0 1px 3px rgba(0,0,0,0.4);
            transition: transform var(--transition), background var(--transition);
        }
        .switch-row input[type="checkbox"]:checked {
            background: var(--primary); border-color: var(--primary);
            box-shadow: 0 0 12px rgba(99,102,241,0.25);
        }
        .switch-row input[type="checkbox"]:checked::after {
            transform: translateX(1.25rem); background: white;
        }

        /* Range Slider */
        #product-app input[type="range"] {
            -webkit-appearance: none; appearance: none;
            height: 5px; border-radius: 3px;
            background: rgba(255,255,255,0.1);
            border: none; outline: none; cursor: pointer;
            padding: 0; min-height: 0;
        }
        #product-app input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none; width: 18px; height: 18px;
            border-radius: 50%; background: white;
            box-shadow: 0 1px 4px rgba(0,0,0,0.35), 0 0 0 2px var(--primary);
            cursor: pointer; transition: box-shadow var(--transition);
        }
        #product-app input[type="range"]::-webkit-slider-thumb:hover {
            box-shadow: 0 1px 6px rgba(0,0,0,0.35), 0 0 0 2px var(--primary), 0 0 14px var(--primary-glow);
        }
        #product-app input[type="range"]::-moz-range-thumb {
            width: 18px; height: 18px; border: 2px solid var(--primary);
            border-radius: 50%; background: white;
            box-shadow: 0 1px 4px rgba(0,0,0,0.35); cursor: pointer;
        }
        #product-app input[type="range"]::-moz-range-track {
            height: 5px; border-radius: 3px; background: rgba(255,255,255,0.1); border: none;
        }

        /* Other checkboxes (not in switch-row) */
        #product-app input[type="checkbox"]:not(.switch-row input) { accent-color: #818cf8; }

        /* ══════════════════════════════════════════════════════
           BUTTONS
           ══════════════════════════════════════════════════════ */
        button {
            font-size: 0.85rem; font-weight: 600;
            border-radius: var(--radius-sm); padding: 0.65rem 1rem;
            border: 1px solid var(--border);
            color: var(--text); background: rgba(255,255,255,0.06);
            cursor: pointer;
            transition: all var(--transition);
            -webkit-tap-highlight-color: transparent;
        }
        button:hover { background: rgba(255,255,255,0.12); }
        button:active { transform: scale(0.97); }
        .button-primary {
            border-color: transparent;
            background: linear-gradient(135deg, #6366f1, #4f46e5);
            box-shadow: 0 2px 8px rgba(99,102,241,0.3);
            font-weight: 700;
        }
        .button-primary:hover {
            filter: brightness(1.12);
            box-shadow: 0 4px 16px rgba(99,102,241,0.4);
        }
        .button-secondary {
            border-radius: var(--radius-sm); padding: 0.65rem 0.85rem;
            border: 1px solid var(--border); color: var(--text);
            background: rgba(255,255,255,0.06); cursor: pointer;
        }
        .button-secondary:hover { background: rgba(255,255,255,0.12); }
        .button-danger {
            color: #fecaca;
            border-color: rgba(248,113,113,0.35);
            background: var(--danger-bg);
            box-shadow: 0 0 0 0 var(--danger-glow);
            animation: danger-pulse 3s ease-in-out infinite;
        }
        .button-danger:hover {
            background: rgba(153, 27, 27, 0.5);
            box-shadow: 0 0 20px var(--danger-glow);
            animation: none;
        }
        @keyframes danger-pulse {
            0%, 100% { box-shadow: 0 0 0 0 rgba(248,113,113,0); }
            50% { box-shadow: 0 0 0 4px rgba(248,113,113,0.08); }
        }

        /* ══════════════════════════════════════════════════════
           APP SHELL
           ══════════════════════════════════════════════════════ */
        #product-app { padding: 1.25rem; max-width: 88rem; margin: 0 auto; }
        #product-app button, #product-app input, #product-app select { width: auto; margin: 0; }
        #product-app input, #product-app select {
            min-height: 2.5rem; padding: 0.55rem 0.75rem;
            background: rgba(5,6,15,.55);
        }
        .app-shell { display: grid; gap: 1rem; }
        .app-header {
            display: flex; flex-wrap: wrap; gap: 0.75rem;
            align-items: center; justify-content: space-between;
            padding: 1rem 1.25rem;
            border: 1px solid var(--border);
            border-radius: var(--radius-lg);
            background: rgba(10, 12, 24, 0.82);
            backdrop-filter: blur(16px);
            box-shadow: 0 4px 16px rgba(0,0,0,0.2);
        }
        .brand { display: flex; align-items: center; gap: 0.75rem; min-width: 0; }
        .brand-mark {
            width: 2.5rem; height: 2.5rem;
            display: grid; place-items: center;
            border-radius: 0.7rem;
            background: linear-gradient(135deg, #818cf8, #34d399);
            box-shadow: 0 4px 12px rgba(99,102,241,0.35);
            overflow: hidden;
            animation: brand-glow 4s ease-in-out infinite alternate;
        }
        @keyframes brand-glow {
            0% { box-shadow: 0 4px 12px rgba(99,102,241,0.35); }
            100% { box-shadow: 0 4px 20px rgba(52,211,153,0.35); }
        }
        .brand-mark svg { width: 22px; height: 22px; }
        .brand h2 { margin: 0; font-size: 1rem; letter-spacing: 0.08em; }
        .online-pill {
            display: inline-flex; align-items: center; gap: 0.4rem;
            font-size: 0.7rem; color: #bbf7d0; font-weight: 600;
        }
        .online-pill::before {
            content: ""; width: 0.45rem; height: 0.45rem;
            border-radius: 50%; background: #34d399;
            box-shadow: 0 0 6px #34d399;
            animation: pulse-dot 2s ease-in-out infinite;
        }
        @keyframes pulse-dot {
            0%, 100% { box-shadow: 0 0 4px #34d399; opacity: 1; }
            50% { box-shadow: 0 0 12px #34d399, 0 0 20px rgba(52,211,153,0.3); opacity: 0.8; }
        }
        .header-actions { display: flex; align-items: center; gap: 0.45rem; }
        .header-actions button { font-size: 0.78rem; padding: 0.55rem 0.75rem; }
        #live-preview {
            width: 100%; image-rendering: pixelated;
            border-radius: 6px; background: rgba(5,6,15,0.6);
            margin-top: 0.5rem; display: none;
            border: 1px solid rgba(255,255,255,0.06);
            box-shadow: 0 0 20px rgba(99,102,241,0.08);
        }

        /* ══════════════════════════════════════════════════════
           NAVIGATION RAIL
           ══════════════════════════════════════════════════════ */
        .nav-rail {
            display: flex; flex-wrap: wrap; gap: 0.35rem;
            padding: 0.35rem;
            background: rgba(10, 12, 24, 0.65);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            position: relative;
        }
        .nav-rail button {
            display: inline-flex; align-items: center; gap: 0.5rem;
            border: 0; background: transparent;
            color: var(--text-muted); border-radius: 0.6rem;
            padding: 0.6rem 0.85rem; cursor: pointer;
            font-weight: 600; font-size: 0.82rem;
            transition: color var(--transition), background var(--transition), box-shadow var(--transition);
        }
        .nav-rail button svg {
            width: 16px; height: 16px; opacity: 0.6;
            transition: opacity var(--transition);
            flex-shrink: 0;
        }
        .nav-rail button:hover { color: var(--text-secondary); background: rgba(255,255,255,0.04); }
        .nav-rail button:hover svg { opacity: 0.8; }
        .nav-rail button.active {
            color: white; background: rgba(129,140,248,0.18);
            box-shadow: inset 0 0 0 1px rgba(165,180,252,0.2);
        }
        .nav-rail button.active svg { opacity: 1; }

        /* ══════════════════════════════════════════════════════
           PAGES & TRANSITIONS
           ══════════════════════════════════════════════════════ */
        .product-page { display: none; animation: page-in 0.3s var(--ease) both; }
        .product-page.active { display: block; }
        @keyframes page-in {
            from { opacity: 0; transform: translateY(8px); }
            to   { opacity: 1; transform: none; }
        }
        .page-heading {
            display: flex; justify-content: space-between; align-items: flex-end;
            gap: 1rem; margin: 0.5rem 0 1rem; flex-wrap: wrap;
        }
        .page-heading h3 { margin: 0; }

        /* ══════════════════════════════════════════════════════
           CARDS
           ══════════════════════════════════════════════════════ */
        .product-card {
            padding: 1.25rem; border-radius: var(--radius-lg);
            border: 1px solid var(--border);
            background: var(--surface);
            backdrop-filter: blur(10px);
            box-shadow:
                0 2px 8px rgba(0,0,0,0.12),
                0 0 0 0.5px rgba(255,255,255,0.03) inset;
            transition: border-color var(--transition), box-shadow var(--transition), transform var(--transition);
        }
        .product-card:hover {
            border-color: rgba(255,255,255,0.1);
        }
        .product-card h4 { margin: 0 0 0.5rem; font-size: 0.95rem; }
        .product-card p { margin: 0.35rem 0 0; }

        /* ══════════════════════════════════════════════════════
           CONTROL ROOM
           ══════════════════════════════════════════════════════ */
        .overview-grid { display: grid; grid-template-columns: 1.1fr 0.9fr; gap: 1rem; }
        .stat-list { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 0.55rem; margin-top: 0.85rem; }
        .stat-list div {
            padding: 0.65rem; border-radius: var(--radius-sm);
            background: rgba(0,0,0,0.2);
            border: 1px solid var(--border-subtle);
            transition: border-color var(--transition);
        }
        .stat-list div:hover { border-color: var(--border); }
        .stat-list span {
            display: block; color: var(--text-muted);
            font-size: 0.65rem; text-transform: uppercase;
            letter-spacing: 0.08em; font-weight: 600;
        }
        .stat-list strong { font-size: 0.95rem; font-variant-numeric: tabular-nums; }

        /* ── Scene Cards ── */
        .scene-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(14rem, 1fr)); gap: 1rem; }
        .scene-card {
            display: grid; gap: 0.75rem; min-height: 10rem;
            border-top: 3px solid var(--scene-color, var(--primary));
            transition: transform var(--transition), box-shadow var(--transition), border-color var(--transition);
        }
        .scene-card:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 24px rgba(0,0,0,0.25), 0 0 20px rgba(99,102,241,0.06);
        }
        .scene-card .scene-actions { display: flex; gap: 0.5rem; margin-top: auto; }
        .scene-card .scene-actions button { flex: 1; }
        .scene-chip {
            display: inline-flex; align-items: center; gap: 0.35rem;
            color: #c7d2fe; font-size: 0.72rem; font-weight: 700;
        }

        /* ══════════════════════════════════════════════════════
           SCENE EDITOR
           ══════════════════════════════════════════════════════ */
        .editor-toolbar, .timeline-toolbar, .cue-toolbar {
            display: flex; justify-content: space-between; align-items: center;
            gap: 0.75rem; flex-wrap: wrap; margin-bottom: 1rem;
        }
        .editor-toolbar > div, .timeline-toolbar > div, .cue-toolbar > div {
            display: flex; align-items: center; gap: 0.6rem; flex-wrap: wrap;
        }
        .range-row {
            display: grid; grid-template-columns: minmax(7rem, 1fr) minmax(8rem, 2fr) minmax(3.2rem, auto);
            gap: 0.65rem; align-items: center; margin-top: 0.8rem;
        }
        .range-row label { font-size: 0.78rem; font-weight: 700; color: var(--text-secondary); }
        .range-row output { color: #c7d2fe; font-variant-numeric: tabular-nums; text-align: right; font-weight: 600; font-size: 0.85rem; }
        .color-line { display: flex; justify-content: space-between; align-items: center; gap: 0.7rem; }
        .color-line input[type="color"] { width: 3.5rem !important; height: 2.2rem; padding: 0.15rem !important; border-radius: var(--radius-sm); cursor: pointer; }
        .zone-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(16rem, 1fr)); gap: 1rem; }
        .zone-card { display: grid; gap: 0.65rem; }
        .zone-title { display: flex; justify-content: space-between; align-items: center; }
        .zone-title span { color: var(--text-muted); font-size: 0.72rem; }
        .overlay-controls { display: grid; gap: 0.6rem; }
        .settings-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(17rem, 1fr)); gap: 1rem; }

        /* Layer editor */
        .layer-editor {
            margin-top: 1.25rem; border-top: 1px solid var(--border);
            padding-top: 1.25rem; position: relative;
        }
        .layer-editor h5 {
            font-size: 0.82rem; font-weight: 700;
            color: var(--text-secondary); margin-bottom: 0.75rem;
            display: flex; align-items: center; gap: 0.5rem;
        }
        .layer-editor h5::before {
            content: ''; width: 8px; height: 8px;
            border-radius: 2px; background: var(--primary); opacity: 0.6;
        }

        /* ══════════════════════════════════════════════════════
           TIMELINE
           ══════════════════════════════════════════════════════ */
        .palette {
            display: flex; gap: 0.5rem; overflow-x: auto;
            padding-bottom: 0.4rem;
            scrollbar-width: thin; scrollbar-color: rgba(255,255,255,0.1) transparent;
        }
        .palette button {
            min-width: 8.5rem; text-align: left;
            border-radius: var(--radius-sm); padding: 0.65rem;
            border: 1px solid var(--border);
            background: rgba(255,255,255,0.04);
            color: var(--text); cursor: grab;
            transition: border-color var(--transition), background var(--transition), box-shadow var(--transition);
        }
        .palette button:hover { background: rgba(255,255,255,0.08); }
        .palette button.selected {
            border-color: #818cf8; background: rgba(99,102,241,0.15);
            box-shadow: 0 0 12px rgba(99,102,241,0.12);
        }
        .palette button strong { display: block; font-size: 0.82rem; }
        .palette button span { display: block; font-size: 0.7rem; color: var(--text-muted); margin-top: 0.15rem; }
        .track {
            display: grid; grid-template-columns: 9rem minmax(36rem, 1fr);
            gap: 0.65rem; align-items: center; margin: 0.55rem 0;
        }
        .track-head {
            display: grid; gap: 0.35rem; padding: 0.55rem;
            border-radius: var(--radius-sm);
            background: rgba(255,255,255,0.04);
            border: 1px solid var(--border-subtle);
        }
        .track-head strong { font-size: 0.78rem; }
        .track-head select { width: 100% !important; min-height: 2rem !important; font-size: 0.72rem; }
        .step-lane {
            display: grid;
            grid-template-columns: repeat(16, minmax(3.4rem, 1fr));
            gap: 0.25rem;
        }
        .step {
            min-height: 3.5rem; overflow: hidden;
            border-radius: 0.45rem;
            border: 1px solid rgba(255,255,255,0.08);
            background: rgba(255,255,255,0.03);
            color: var(--text-muted); cursor: pointer;
            padding: 0.3rem; text-align: left;
            transition: border-color var(--transition), background var(--transition), box-shadow var(--transition);
        }
        .step:hover { background: rgba(255,255,255,0.06); border-color: rgba(255,255,255,0.12); }
        .step span { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 0.63rem; }
        .step small { color: #a5b4fc; font-size: 0.57rem; font-weight: 700; }
        .step.selected {
            border-color: #818cf8;
            box-shadow: 0 0 0 2px rgba(129,140,248,0.2), 0 0 12px rgba(99,102,241,0.1);
            background: rgba(99,102,241,0.08);
        }
        .step.inactive { opacity: 0.3; }
        .timeline-scroll { overflow-x: auto; padding-bottom: 0.5rem; }
        .step-inspector { margin-top: 1rem; display: grid; grid-template-columns: 1fr auto; gap: 1rem; align-items: end; }
        .beat-options { display: flex; gap: 0.3rem; }
        .beat-options button {
            padding: 0.45rem 0.65rem; border: 1px solid var(--border);
            border-radius: 0.4rem; color: var(--text-muted);
            background: transparent; cursor: pointer;
            font-variant-numeric: tabular-nums; font-weight: 600;
            transition: all var(--transition);
        }
        .beat-options button:hover { background: rgba(255,255,255,0.06); }
        .beat-options button.active {
            color: white; background: rgba(99,102,241,0.22);
            border-color: #818cf8;
        }

        /* ══════════════════════════════════════════════════════
           AUTOMATION
           ══════════════════════════════════════════════════════ */
        .cue-list { display: grid; gap: 0.6rem; }
        .cue {
            display: grid;
            grid-template-columns: auto 7rem 1fr 1.3fr auto;
            align-items: center; gap: 0.65rem;
            padding: 0.8rem;
            border: 1px solid var(--border);
            border-radius: var(--radius);
            background: rgba(255,255,255,0.025);
            transition: border-color var(--transition), background var(--transition);
        }
        .cue:hover { border-color: rgba(255,255,255,0.1); background: rgba(255,255,255,0.04); }
        .days { display: flex; gap: 0.2rem; }
        .days button {
            border: 1px solid var(--border); border-radius: 0.35rem;
            background: transparent; color: var(--text-muted);
            padding: 0.3rem 0.4rem; font-size: 0.65rem;
            cursor: pointer; font-weight: 700;
            transition: all var(--transition);
        }
        .days button:hover { background: rgba(255,255,255,0.06); }
        .days button.active {
            color: white;
            background: linear-gradient(135deg, #4f46e5, #6366f1);
            border-color: #6366f1;
            box-shadow: 0 2px 6px rgba(99,102,241,0.25);
        }

        /* ══════════════════════════════════════════════════════
           SYSTEM PAGE
           ══════════════════════════════════════════════════════ */
        .sync-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(12rem, 1fr)); gap: 1rem; }
        .node-card { min-height: 7.5rem; }
        .node-card.primary {
            border-color: rgba(52,211,153,0.4);
            background: linear-gradient(145deg, rgba(16,185,129,0.12), var(--surface));
        }
        .node-card .latency {
            color: #a7f3d0; font-size: 1.2rem; font-weight: 800;
            margin-top: 0.7rem; font-variant-numeric: tabular-nums;
        }

        /* Collapsible hardware sections */
        .section-header {
            display: flex; justify-content: space-between; align-items: center;
            cursor: pointer; user-select: none;
            padding: 0.15rem 0; margin-bottom: 0.4rem;
            transition: opacity var(--transition);
        }
        .section-header:hover { opacity: 0.8; }
        .section-header h4 { margin: 0; }
        .section-chevron {
            color: var(--text-muted); font-size: 1rem;
            transition: transform var(--transition);
            line-height: 1;
        }
        .hw-section { transition: opacity 0.15s; }
        .hw-section.collapsed { display: none; }

        /* Hardware config field cards */
        .hw-field {
            margin-bottom: 0.4rem; padding: 0.65rem;
            background: rgba(0,0,0,0.2); border-radius: var(--radius-sm);
            border: 1px solid var(--border-subtle);
            transition: border-color var(--transition);
        }
        .hw-field:hover { border-color: var(--border); }
        .hw-field-head {
            font-weight: 700; margin-bottom: 0.4rem; font-size: 0.85rem;
            display: flex; justify-content: space-between; align-items: center;
        }

        /* ══════════════════════════════════════════════════════
           EMPTY STATE
           ══════════════════════════════════════════════════════ */
        .empty-state {
            padding: 2.5rem; text-align: center; color: var(--text-muted);
            border: 1px dashed rgba(255,255,255,0.08);
            border-radius: var(--radius);
            background: rgba(255,255,255,0.015);
            font-weight: 500;
        }

        /* ══════════════════════════════════════════════════════
           TOAST NOTIFICATIONS
           ══════════════════════════════════════════════════════ */
        #experience-toast {
            position: fixed; z-index: 50;
            right: 1.25rem; bottom: 1.25rem;
            max-width: min(26rem, calc(100vw - 2.5rem));
            padding: 0.85rem 1.15rem; border-radius: var(--radius);
            background: rgba(15, 23, 42, 0.94);
            backdrop-filter: blur(12px);
            border: 1px solid rgba(255,255,255,0.08);
            border-left: 3px solid var(--accent);
            color: white; font-size: 0.85rem; font-weight: 550;
            box-shadow: 0 8px 24px rgba(0,0,0,0.4), 0 0 0 0.5px rgba(255,255,255,0.05) inset;
            transform: translateY(calc(100% + 3rem));
            opacity: 0;
            transition: transform 0.35s var(--ease), opacity 0.35s;
            overflow: hidden;
        }
        #experience-toast.show { transform: translateY(0); opacity: 1; }
        #experience-toast.error { border-left-color: var(--danger); }
        #experience-toast::after {
            content: ''; position: absolute;
            bottom: 0; left: 0; right: 0; height: 2px;
            background: var(--accent);
            transform-origin: left; transform: scaleX(0);
        }
        #experience-toast.show::after {
            transform: scaleX(1);
            animation: toast-drain 3.5s linear forwards;
        }
        #experience-toast.error::after { background: var(--danger); }
        @keyframes toast-drain {
            from { transform: scaleX(1); }
            to   { transform: scaleX(0); }
        }

        /* ══════════════════════════════════════════════════════
           RESPONSIVE
           ══════════════════════════════════════════════════════ */
        @media (max-width: 52rem) {
            #product-app { padding: 0.65rem; }
            .overview-grid { grid-template-columns: 1fr; }
            .app-header { align-items: flex-start; }
            .track { grid-template-columns: 1fr; }
            .cue { grid-template-columns: 1fr 1fr; }
            .cue .days { grid-column: 1 / -1; }
            .step-inspector { grid-template-columns: 1fr; }
            .nav-rail {
                position: fixed; bottom: 0; left: 0; right: 0;
                z-index: 40; border-radius: var(--radius-lg) var(--radius-lg) 0 0;
                justify-content: center;
                background: rgba(10, 12, 24, 0.92);
                backdrop-filter: blur(16px);
                padding: 0.4rem 0.6rem calc(0.4rem + env(safe-area-inset-bottom, 0));
                box-shadow: 0 -4px 20px rgba(0,0,0,0.3);
                border-top: 1px solid var(--border);
            }
            .nav-rail button { flex-direction: column; gap: 0.15rem; padding: 0.5rem 0.6rem; font-size: 0.6rem; }
            .nav-rail button svg { width: 20px; height: 20px; }
            .app-shell { padding-bottom: calc(4.5rem + env(safe-area-inset-bottom, 0)); }
        }
        @media (max-width: 36rem) {
            .settings-grid { grid-template-columns: 1fr; }
            .scene-grid { grid-template-columns: 1fr; }
        }

        /* ── Scrollbar ── */
        ::-webkit-scrollbar { width: 6px; height: 6px; }
        ::-webkit-scrollbar-track { background: transparent; }
        ::-webkit-scrollbar-thumb { background: rgba(255,255,255,0.1); border-radius: 3px; }
        ::-webkit-scrollbar-thumb:hover { background: rgba(255,255,255,0.18); }

        /* ── Focus visible ── */
        :focus-visible { outline: 2px solid var(--primary); outline-offset: 2px; }

        /* ── Utility ── */
        .sr-only { position: absolute; width: 1px; height: 1px; padding: 0; margin: -1px; overflow: hidden; clip: rect(0,0,0,0); border: 0; }
    </style>
</head>
<body>

<section id="experience-auth" aria-labelledby="login-title">
    <div class="experience-login">
        <form onsubmit="lg.connect(); return false;">
        <div class="login-logo">
            <svg width="36" height="36" viewBox="0 0 36 36" fill="none" aria-hidden="true">
                <defs><linearGradient id="lg" x1="0" y1="0" x2="36" y2="36"><stop stop-color="#818cf8"/><stop offset="1" stop-color="#34d399"/></linearGradient></defs>
                <rect x="2" y="2" width="8" height="8" rx="2.5" fill="url(#lg)"/>
                <rect x="14" y="2" width="8" height="8" rx="2.5" fill="url(#lg)" opacity=".65"/>
                <rect x="26" y="2" width="8" height="8" rx="2.5" fill="url(#lg)" opacity=".35"/>
                <rect x="2" y="14" width="8" height="8" rx="2.5" fill="url(#lg)" opacity=".65"/>
                <rect x="14" y="14" width="8" height="8" rx="2.5" fill="url(#lg)"/>
                <rect x="26" y="14" width="8" height="8" rx="2.5" fill="url(#lg)" opacity=".65"/>
                <rect x="2" y="26" width="8" height="8" rx="2.5" fill="url(#lg)" opacity=".35"/>
                <rect x="14" y="26" width="8" height="8" rx="2.5" fill="url(#lg)" opacity=".65"/>
                <rect x="26" y="26" width="8" height="8" rx="2.5" fill="url(#lg)"/>
            </svg>
            <div>
                <p class="eyebrow">LumiGrid lighting control</p>
            </div>
        </div>
        <h1 id="login-title">Make light<br>feel alive.</h1>
        <p class="muted">Connect securely to this node to arrange scenes, timelines, and automation.</p>
        <input type="text" autocomplete="username" tabindex="-1" aria-hidden="true" hidden>
        <label for="experience-token">Node access token</label>
        <input id="experience-token" type="password" autocomplete="current-password" placeholder="Enter the token supplied with this node">
        <button class="button-primary" type="submit" style="width:100%;margin-top:1.25rem">Connect</button>
        </form>
    </div>
</section>

<main id="product-app" hidden>
    <div class="app-shell">
        <header class="app-header">
            <div style="width: 100%; display: flex; justify-content: space-between; align-items: center;">
                <div class="brand">
                    <div class="brand-mark" aria-hidden="true">
                        <svg width="22" height="22" viewBox="0 0 22 22" fill="none">
                            <rect x="1" y="1" width="5" height="5" rx="1.5" fill="white" opacity=".9"/>
                            <rect x="8.5" y="1" width="5" height="5" rx="1.5" fill="white" opacity=".5"/>
                            <rect x="16" y="1" width="5" height="5" rx="1.5" fill="white" opacity=".25"/>
                            <rect x="1" y="8.5" width="5" height="5" rx="1.5" fill="white" opacity=".5"/>
                            <rect x="8.5" y="8.5" width="5" height="5" rx="1.5" fill="white" opacity=".9"/>
                            <rect x="16" y="8.5" width="5" height="5" rx="1.5" fill="white" opacity=".5"/>
                            <rect x="1" y="16" width="5" height="5" rx="1.5" fill="white" opacity=".25"/>
                            <rect x="8.5" y="16" width="5" height="5" rx="1.5" fill="white" opacity=".5"/>
                            <rect x="16" y="16" width="5" height="5" rx="1.5" fill="white" opacity=".9"/>
                        </svg>
                    </div>
                    <div>
                        <p class="eyebrow">LumiGrid OS</p>
                        <h2 id="experience-node-name">LIGHTING NODE</h2>
                        <span class="online-pill">Connected</span>
                    </div>
                </div>
                <div class="header-actions">
                    <button title="Refresh node status" onclick="lg.refresh()">Refresh</button>
                    <button title="End this browser session" onclick="lg.disconnect()">Disconnect</button>
                </div>
            </div>
            <canvas id="live-preview" height="12"></canvas>
        </header>

        <nav class="nav-rail" aria-label="Lighting controls">
            <button class="active" data-page="control" onclick="lg.nav('control', this)">
                <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><line x1="3" y1="4" x2="13" y2="4"/><line x1="3" y1="8" x2="13" y2="8"/><line x1="3" y1="12" x2="13" y2="12"/><circle cx="6" cy="4" r="1.5" fill="currentColor" stroke="none"/><circle cx="10" cy="8" r="1.5" fill="currentColor" stroke="none"/><circle cx="7" cy="12" r="1.5" fill="currentColor" stroke="none"/></svg>
                Control
            </button>
            <button data-page="scenes" onclick="lg.nav('scenes', this)">
                <svg viewBox="0 0 16 16" fill="currentColor"><rect x="1" y="1" width="6" height="6" rx="1.5"/><rect x="9" y="1" width="6" height="6" rx="1.5" opacity=".45"/><rect x="1" y="9" width="6" height="6" rx="1.5" opacity=".45"/><rect x="9" y="9" width="6" height="6" rx="1.5"/></svg>
                Scenes
            </button>
            <button data-page="timeline" onclick="lg.nav('timeline', this)">
                <svg viewBox="0 0 16 16" fill="currentColor"><rect x="1" y="9" width="3" height="6" rx="1"/><rect x="5.5" y="5" width="3" height="10" rx="1" opacity=".7"/><rect x="10" y="2" width="3" height="13" rx="1"/></svg>
                Timeline
            </button>
            <button data-page="automation" onclick="lg.nav('automation', this)">
                <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><circle cx="8" cy="8" r="6"/><path d="M8 4.5v3.5l2.5 1.5"/></svg>
                Automation
            </button>
            <button data-page="system" onclick="lg.nav('system', this)">
                <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><circle cx="8" cy="8" r="2.5"/><path d="M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M12.6 3.4l-1.4 1.4M4.8 11.2L3.4 12.6"/></svg>
                System
            </button>
        </nav>

        <section id="page-control" class="product-page active">
            <div class="page-heading">
                <div><p class="eyebrow">Now playing</p><h3>Control room</h3></div>
                <button class="button-danger" onclick="lg.blackout()">Emergency blackout</button>
            </div>
            <div class="overview-grid">
                <article class="product-card">
                    <h4 id="now-playing">Loading scenes…</h4>
                    <p id="now-playing-copy" class="muted">The node is checking its playback state.</p>
                    <div id="status-stats" class="stat-list"></div>
                </article>
                <article class="product-card">
                    <h4>Sync health</h4>
                    <p id="sync-summary" class="muted">Checking the room clock…</p>
                    <div id="sync-stats" class="stat-list"></div>
                </article>
            </div>
            <div class="page-heading" style="margin-top:1.5rem">
                <div><p class="eyebrow">One tap scenes</p><h3>Set the mood</h3></div>
                <button class="button-secondary" onclick="lg.nav('scenes', document.querySelector('[data-page=scenes]'))">Manage scenes</button>
            </div>
            <div id="control-scenes" class="scene-grid"></div>
        </section>

        <section id="page-scenes" class="product-page">
            <div class="page-heading">
                <div><p class="eyebrow">Scene library</p><h3>Looks, not numbers</h3></div>
                <button class="button-secondary" onclick="lg.refresh()">Refresh library</button>
            </div>
            <div id="scene-library" class="scene-grid"></div>
        </section>

        <section id="page-scene-editor" class="product-page">
            <div class="editor-toolbar">
                <div><button class="button-secondary" onclick="lg.backToScenes()">← Scenes</button><div><p class="eyebrow">Scene designer</p><h3 id="scene-editor-title">Edit scene</h3></div></div>
                <button class="button-primary" onclick="lg.saveScene()">Save scene</button>
            </div>
            <article class="product-card">
                <div class="settings-grid">
                    <div class="field"><label for="scene-name">Scene name</label><input id="scene-name" maxlength="23" placeholder="Store open"></div>
                    <div>
                        <div class="range-row"><label for="scene-brightness">Room brightness</label><input id="scene-brightness" type="range" min="0" max="100" oninput="lg.updateText('scene-brightness-output', this.value + '%')"><output id="scene-brightness-output"></output></div>
                        <div class="range-row"><label for="scene-fade">Scene fade</label><input id="scene-fade" type="range" min="0" max="5000" step="100" oninput="lg.updateText('scene-fade-output', (this.value / 1000).toFixed(1) + ' s')"><output id="scene-fade-output"></output></div>
                    </div>
                </div>
                <div class="overlay-controls" style="margin-top:1rem">
                    <div class="field"><label for="scene-overlay">Layered finishing effect</label><select id="scene-overlay" onchange="lg.changeOverlay(this.value)"><option value="none">No overlay</option><option value="strobe">Beat strobe overlay</option></select></div>
                    <div id="scene-overlay-options"></div>
                </div>
            </article>
            <p class="help" style="margin:1rem 0">Each zone starts with a base look. Add the strobe overlay to make any scene pulse without replacing its color or motion.</p>
            <div id="scene-zones" class="zone-grid"></div>
        </section>

        <section id="page-timeline" class="product-page">
            <div class="page-heading">
                <div><p class="eyebrow">Performance timeline</p><h3>Build the room's rhythm</h3></div>
                <button class="button-primary" onclick="lg.saveTimeline()">Save timeline</button>
            </div>
            <article class="product-card">
                <div class="timeline-toolbar">
                    <div>
                        <strong>Tempo</strong>
                        <input id="timeline-bpm" type="range" min="40" max="240" oninput="lg.setBpm(this.value)">
                        <output id="timeline-bpm-output"></output>
                    </div>
                    <label class="switch-row"><span>Play timeline</span><input id="timeline-enabled" type="checkbox" onchange="lg.setTimelineEnabled(this.checked)"></label>
                </div>
                <p class="help">Pick a scene below, then click or drag it onto a step. Every one of the eight lighting zones loops its own sixteen-step lane.</p>
                <div id="timeline-palette" class="palette" aria-label="Scene palette"></div>
            </article>
            <article class="product-card timeline-scroll">
                <div id="timeline-tracks"></div>
                <div id="step-inspector"></div>
            </article>
        </section>

        <section id="page-automation" class="product-page">
            <div class="page-heading">
                <div><p class="eyebrow">Calendar automation</p><h3>Let the room run itself</h3></div>
                <div class="cue-toolbar"><button class="button-secondary" onclick="lg.addCue()">Add cue</button><button class="button-primary" onclick="lg.saveAutomation()">Save automation</button></div>
            </div>
            <article class="product-card">
                <p class="help">Schedule a scene or start the timeline at a local time. A scene cue takes over from a calendar-started timeline.</p>
                <div id="cue-list" class="cue-list" style="margin-top:1rem"></div>
            </article>
        </section>

        <section id="page-system" class="product-page">
            <div class="page-heading">
                <div><p class="eyebrow">Node and room health</p><h3>System</h3></div>
                <button class="button-primary" onclick="lg.saveSystem()">Save settings</button>
            </div>
            <div class="settings-grid">
                <article class="product-card">
                    <h4>Node preferences</h4>
                    <div class="field"><label for="system-name">Node name</label><input id="system-name" maxlength="31"></div>
                    <div class="field"><label for="system-fallback">Safe fallback scene</label><select id="system-fallback"></select></div>
                    <div class="field"><label for="system-timezone">Timezone</label><input id="system-timezone" placeholder="UTC0"></div>
                </article>
                <article class="product-card">
                    <h4>Room sync</h4>
                    <label class="switch-row"><span>Sync this node with the room</span><input id="system-sync-enabled" type="checkbox"></label>
                    <div class="field"><label for="system-sync-role">Room role</label><select id="system-sync-role"><option value="0">Automatic leader selection</option><option value="1">Lead the room</option><option value="2">Follow the room</option></select></div>
                    <p class="help">Role changes are applied the next time the node starts. Automatic mode elects one node instead of broadcasting from every node.</p>
                </article>
                <article class="product-card">
                    <h4>MQTT Configuration</h4>
                    <label class="switch-row"><span>Enable MQTT</span><input id="system-mqtt-enabled" type="checkbox"></label>
                    <div class="field"><label for="system-mqtt-broker">MQTT Broker Address</label><input id="system-mqtt-broker" placeholder="192.168.1.100" maxlength="63"></div>
                    <div class="field"><label for="system-mqtt-port">MQTT Port</label><input id="system-mqtt-port" type="number" value="1883" min="1" max="65535"></div>
                    <p class="help">When enabled, the node will publish telemetry and respond to commands via MQTT.</p>
                </article>
                <article class="product-card" style="grid-column: 1 / -1;">
                    <div class="section-header" onclick="lg.toggleHwSection('system-outputs')">
                        <h4>Hardware Outputs</h4>
                        <span class="section-chevron" id="system-outputs-chevron">▸</span>
                    </div>
                    <p class="help">Configure physical output pins (e.g. GPIOs) on your controller.</p>
                    <div id="system-outputs" class="zone-grid hw-section collapsed" style="margin-top:1rem; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));"></div>
                </article>
                <article class="product-card" style="grid-column: 1 / -1;">
                    <div class="section-header" onclick="lg.toggleHwSection('system-fixtures')">
                        <h4>LED Fixtures</h4>
                        <span class="section-chevron" id="system-fixtures-chevron">▸</span>
                    </div>
                    <p class="help">Define LED strips or panels and assign them to physical outputs.</p>
                    <div id="system-fixtures" class="zone-grid hw-section collapsed" style="margin-top:1rem; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));"></div>
                </article>
                <article class="product-card" style="grid-column: 1 / -1;">
                    <h4>Logical Zones (Segments)</h4>
                    <p class="help">Map your LED fixtures to 8 functional zones for scene effects.</p>
                    <div id="system-zones" class="zone-grid" style="margin-top:1rem"></div>
                </article>
            </div>
            <div class="page-heading" style="margin-top:1.5rem"><div><p class="eyebrow">Topology</p><h3>Nodes in this room</h3></div><button class="button-secondary" onclick="lg.refreshSync()">Refresh health</button></div>
            <div id="sync-topology" class="sync-grid"></div>

            <div class="page-heading" style="margin-top:2.5rem"><div><p class="eyebrow">Maintenance</p><h3>Firmware &amp; Config</h3></div></div>
            <div class="settings-grid">
                <article class="product-card">
                    <h4>Over-The-Air Update (OTA)</h4>
                    <p class="help mb-1">Upload a signed firmware binary to update the node.</p>
                    <input type="file" id="ota-file" accept=".bin" style="width: 100%; margin-bottom: 1rem;">
                    <button class="button-primary" style="width: 100%" onclick="lg.uploadOta()">Update Firmware</button>
                    <div id="ota-status" class="help" style="margin-top: 1rem;"></div>
                </article>
                <article class="product-card">
                    <h4>Configuration Export &amp; Import</h4>
                    <p class="help mb-1">Backup or restore device settings and presets.</p>
                    <button class="button-secondary" style="width: 100%" onclick="lg.downloadBackup()">Export Configuration</button>
                    <hr style="border: 0; border-top: 1px solid var(--border); margin: 1.5rem 0;">
                    <input type="file" id="restore-file" accept=".json" style="width: 100%; margin-bottom: 1rem;">
                    <button class="button-primary" style="width: 100%" onclick="lg.uploadRestore()">Restore Configuration</button>
                </article>
            </div>
        </section>
    </div>
</main>
<div id="experience-toast" role="status" aria-live="polite"></div>

<script>
window.lg = (() => {
    const fallbackEffects = [
        {id:'solid',label:'Steady color',description:'A constant, even wash of color.',hasParameter:false},
        {id:'breathe',label:'Smooth pulse',description:'Gently fades between a soft glow and full brightness.',hasParameter:true,parameterLabel:'Minimum glow',parameterUnit:'%',parameterMin:1,parameterMax:100,parameterDefault:4},
        {id:'chase',label:'Moving comet',description:'A bright comet travels along the strip.',hasParameter:true,parameterLabel:'Tail length',parameterUnit:'pixels',parameterMin:1,parameterMax:8,parameterDefault:3},
        {id:'sweep',label:'Color sweep',description:'Builds color across the strip from end to end.',hasParameter:false},
        {id:'reveal',label:'Soft reveal',description:'Fades the lane in for a polished sign-on.',hasParameter:true,parameterLabel:'Fade length',parameterUnit:'%',parameterMin:10,parameterMax:90,parameterDefault:25},
        {id:'sparkle',label:'Random twinkle',description:'Adds crisp white twinkles over the base color.',hasParameter:true,parameterLabel:'Twinkles',parameterUnit:'per lane',parameterMin:1,parameterMax:12,parameterDefault:2},
        {id:'gradient',label:'Flowing glow',description:'Moves a soft wave of light along the strip.',hasParameter:true,parameterLabel:'Wave contrast',parameterUnit:'%',parameterMin:1,parameterMax:100,parameterDefault:100},
        {id:'strobe',label:'Beat strobe',description:'Flashes the lane with an adjustable on-time.',hasParameter:true,parameterLabel:'Flash time',parameterUnit:'%',parameterMin:5,parameterMax:95,parameterDefault:50},
        {id:'fire',label:'Living flame',description:'Creates a flickering flame texture from the selected color.',hasParameter:true,parameterLabel:'Flame height',parameterUnit:'%',parameterMin:20,parameterMax:100,parameterDefault:70},
        {id:'aurora',label:'Aurora glow',description:'Gentle shifting colors across the strip.',hasParameter:true,parameterLabel:'Hue shift',parameterUnit:'deg',parameterMin:1,parameterMax:360,parameterDefault:120},
        {id:'colorwave',label:'Color wave',description:'Smooth sine wave color interpolation.',hasParameter:true,parameterLabel:'Wave length',parameterUnit:'px',parameterMin:10,parameterMax:255,parameterDefault:50},
        {id:'staticpulse',label:'Static pulse',description:'Slow dimmer pulse for main/ambiance lights.',hasParameter:true,parameterLabel:'Min dim',parameterUnit:'%',parameterMin:0,parameterMax:100,parameterDefault:20}
    ];
    const state = { token:'', effects:fallbackEffects, presets:[], sequence:null, schedules:null, config:null, status:null, sync:null, editing:null, selectedScene:null, selectedStep:null };
    const byId = id => document.getElementById(id);
    const clamp = (value, low, high) => Math.max(low, Math.min(high, Number(value) || 0));
    const esc = value => String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
    const clone = value => JSON.parse(JSON.stringify(value));

    function toast(message, error=false) {
        const target = byId('experience-toast');
        target.textContent = message;
        target.className = (error ? 'error ' : '') + 'show';
        window.clearTimeout(toast.timeout);
        toast.timeout = window.setTimeout(() => { target.className = ''; }, 3500);
    }

    async function api(path, method='GET', body) {
        const headers = { Authorization: 'Bearer ' + state.token };
        if (body !== undefined) headers['Content-Type'] = 'application/json';
        const response = await fetch(path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
        if (!response.ok) {
            let message = `Request failed (${response.status})`;
            try { message = (await response.json()).error || message; } catch (_) {}
            throw new Error(message);
        }
        return response.json();
    }

    function blankLayer() {
        return {effect:'solid',blendMode:'normal',primaryColor:{r:255,g:255,b:255},secondaryColor:{r:0,g:0,b:0},speedMs:2000,fxParam:0,paletteId:0,opacity:255,enabled:false};
    }
    function blankZone() {
        return {layers: [blankLayer(), blankLayer(), blankLayer()]};
    }
    function normalPreset(preset) {
        const scene = {...preset};
        scene.zones = Array.from({length:8}, (_, z) => {
            const srcZone = scene.zones?.[z] || {};
            return {
                layers: Array.from({length:3}, (_, l) => ({
                    ...blankLayer(),
                    ...(srcZone.layers?.[l] || {})
                }))
            };
        });
        scene.globalBrightness = clamp(scene.globalBrightness ?? 128, 0, 255);
        scene.transitionMs = clamp(scene.transitionMs ?? 1000, 0, 10000);
        return scene;
    }
    function defaultSequence() {
        return {bpm:120,enabled:false,tracks:Array.from({length:8}, (_, channelId) => ({channelId,enabled:false,numSteps:16,steps:Array.from({length:16}, () => ({presetIndex:0,lengthBeats:1}))}))};
    }
    function normalSequence(sequence) {
        const result = sequence || defaultSequence();
        result.bpm = clamp(result.bpm ?? 120, 10, 500);
        result.tracks = Array.from({length:8}, (_, index) => {
            const track = {...defaultSequence().tracks[index], ...(result.tracks?.[index] || {})};
            track.channelId = index;
            track.numSteps = clamp(track.numSteps ?? 16, 1, 16);
            track.steps = Array.from({length:16}, (_, step) => ({presetIndex:0,lengthBeats:1,...(track.steps?.[step] || {})}));
            return track;
        });
        return result;
    }
    function normalSchedules(payload) {
        const entries = payload?.entries || [];
        return Array.from({length:8}, (_, index) => ({hour:8,minute:0,daysOfWeek:31,action:'preset',presetIndex:0,enabled:false,...(entries[index] || {})}));
    }
    function effect(id) { return state.effects.find(item => item.id === id) || state.effects[0]; }
    function sceneName(index) { return state.presets.find(scene => Number(scene.index) === Number(index))?.name || `Scene ${Number(index) + 1}`; }
    function percentForDuty(duty) { return Math.round(clamp(duty,0,4095) * 100 / 4095); }
    function dutyForPercent(percent) { return Math.round(clamp(percent,0,100) * 4095 / 100); }
    function colorFor(channel) { return '#' + [channel.r,channel.g,channel.b].map(value => clamp(value,0,255).toString(16).padStart(2,'0')).join(''); }
    function msToSpeed(ms) { return Math.round(100 - (Math.log(clamp(ms,10,60000) / 10) / Math.log(6000)) * 99); }
    function speedToMs(value) { return Math.round(10 * Math.pow(6000, (100 - clamp(value,1,100)) / 99)); }
    function speedName(value) { return value > 75 ? 'Fast' : value > 45 ? 'Balanced' : 'Slow'; }
    function clockAge(value) { return value == null ? 'Waiting' : value < 1000 ? 'Just now' : `${Math.round(value / 1000)} s ago`; }

    let ws = null;
    function initWebSocket() {
        if (ws) { ws.close(); ws = null; }
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        ws = new WebSocket(`${proto}//${location.host}/ws`);
        ws.binaryType = 'arraybuffer';
        ws.onopen = () => {};
        ws.onmessage = (event) => {
            if (event.data instanceof ArrayBuffer) {
                const arr = new Uint8Array(event.data);
                if (arr[0] === 76) { renderLivePreview(arr.slice(1)); }
            } else if (typeof event.data === 'string') {
                try { const st = JSON.parse(event.data); state.status = st; } catch(e) {}
            }
        };
        ws.onclose = () => { if (state.token) setTimeout(initWebSocket, 2000); };
    }

    function renderLivePreview(rgbData) {
        const canvas = byId('live-preview');
        if (!canvas) return;
        const numPixels = Math.floor(rgbData.length / 3);
        if (numPixels === 0) return;
        if (canvas.width !== numPixels) {
            canvas.width = numPixels;
            canvas.style.display = 'block';
        }
        const ctx = canvas.getContext('2d');
        const imgData = ctx.createImageData(numPixels, 1);
        for (let i = 0; i < numPixels; i++) {
            imgData.data[i*4] = rgbData[i*3];
            imgData.data[i*4+1] = rgbData[i*3+1];
            imgData.data[i*4+2] = rgbData[i*3+2];
            imgData.data[i*4+3] = 255;
        }
        ctx.putImageData(imgData, 0, 0);
        ctx.drawImage(canvas, 0, 0, numPixels, 1, 0, 1, numPixels, canvas.height - 1);
    }

    async function connect() {
        state.token = byId('experience-token').value.trim();
        if (!state.token) return toast('Enter the node access token first.', true);
        try {
            await refresh();
            byId('experience-auth').hidden = true;
            byId('product-app').hidden = false;
            initWebSocket();
            nav('control', document.querySelector('[data-page=control]'));
            toast('Connected to the node.');
        } catch (error) {
            toast(`Could not connect: ${error.message}`, true);
        }
    }
    function disconnect() {
        state.token = '';
        if (ws) { ws.close(); ws = null; }
        byId('experience-token').value = '';
        byId('product-app').hidden = true;
        byId('experience-auth').hidden = false;
    }
    async function refresh() {
        const effects = await api('/api/v1/effects').catch(() => fallbackEffects);
        const presets = await api('/api/v1/presets');
        const status = await api('/api/v1/status');
        const sequence = await api('/api/v1/sequence');
        const config = await api('/api/v1/config');
        const schedules = await api('/api/v1/schedules');
        const sync = await api('/api/v1/sync').catch(() => null);
        state.effects = Array.isArray(effects) && effects.length ? effects : fallbackEffects;
        state.presets = presets.map(normalPreset);
        state.status = status;
        state.sequence = normalSequence(sequence);
        state.config = config;
        state.schedules = normalSchedules(schedules);
        state.sync = sync;
        byId('experience-node-name').textContent = (config.deviceName || 'Lighting node').toUpperCase();
        renderControl();
        renderScenes();
        renderTimeline();
        renderAutomation();
        renderSystem();
    }
    function nav(page, button) {
        document.querySelectorAll('.product-page').forEach(item => item.classList.remove('active'));
        byId('page-' + page).classList.add('active');
        document.querySelectorAll('.nav-rail button').forEach(item => item.classList.remove('active'));
        if (button) button.classList.add('active');
        if (page === 'control') renderControl();
        if (page === 'scenes') renderScenes();
        if (page === 'timeline') renderTimeline();
        if (page === 'automation') renderAutomation();
        if (page === 'system') renderSystem();
    }

    function renderControl() {
        const active = state.status?.activePreset;
        byId('now-playing').textContent = active === undefined ? 'No scene selected' : sceneName(active);
        byId('now-playing-copy').textContent = state.status?.transitioning ? 'Fading into the selected scene.' : state.status?.fallbackActive ? 'The safety fallback is active.' : 'The room is ready.';
        byId('status-stats').innerHTML = [
            ['Scene', active === undefined ? '—' : sceneName(active)],
            ['Clock', state.status?.timeSynced ? 'Synced' : 'Starting'],
            ['Fade', state.status?.transitioning ? 'In progress' : 'Ready'],
            ['Safety', state.status?.fallbackActive ? 'Fallback' : 'Normal']
        ].map(([label,value]) => `<div><span>${esc(label)}</span><strong>${esc(value)}</strong></div>`).join('');
        byId('sync-summary').textContent = !state.sync ? 'Sync is not running on this node.' : state.sync.clockLocked ? `Clock locked as ${state.sync.activeRole}.` : 'Waiting for a room leader.';
        byId('sync-stats').innerHTML = state.sync ? [
            ['Role', state.sync.activeRole],
            ['Peers', state.sync.peerCount],
            ['Packet', clockAge(state.sync.lastPacketAgeMs)],
            ['Jitter', `${state.sync.clockJitterUs || 0} μs`]
        ].map(([label,value]) => `<div><span>${esc(label)}</span><strong>${esc(value)}</strong></div>`).join('') : '<div><span>Room sync</span><strong>Off</strong></div>';
        byId('control-scenes').innerHTML = state.presets.length ? state.presets.map(sceneCard).join('') : '<div class="empty-state">No scenes are stored yet.</div>';
    }
    function sceneCard(scene) {
        const firstEffect = effect(scene.zones[0].layers[0].effect);
        const pc = scene.zones[0].layers[0].primaryColor;
        const sceneColor = colorFor(pc);
        return `<article class="product-card scene-card" style="--scene-color:${sceneColor}"><div><span class="scene-chip">● ${esc(firstEffect.label)}</span><h4>${esc(scene.name || 'Untitled scene')}</h4><p class="muted">${Math.round(scene.globalBrightness * 100 / 255)}% brightness · ${Math.round(scene.transitionMs / 100) / 10}s fade</p></div><p class="help">${esc(firstEffect.description)}</p><div class="scene-actions"><button class="button-secondary" onclick="lg.playScene(${Number(scene.index)})">Play</button><button class="button-secondary" onclick="lg.editScene(${Number(scene.index)})">Edit</button></div></article>`;
    }
    function renderScenes() {
        byId('scene-library').innerHTML = state.presets.length ? state.presets.map(sceneCard).join('') : '<div class="empty-state">No scenes are stored yet.</div>';
    }
    async function playScene(index) {
        try {
            await api('/api/v1/activate', 'POST', {preset:index});
            state.status = {...(state.status || {}),activePreset:index,transitioning:true};
            renderControl();
            toast(`${sceneName(index)} is playing.`);
        } catch (error) { toast(error.message, true); }
    }
    async function blackout() {
        try { await api('/api/v1/factory/abort', 'POST'); toast('Emergency blackout requested.'); }
        catch (error) { toast(error.message, true); }
    }

    function editScene(index) {
        const existing = state.presets.find(scene => Number(scene.index) === Number(index));
        if (!existing) return toast('That scene is no longer available.', true);
        state.editing = clone(existing);
        state.selectedStep = null;
        state.targetZones = [true, false, false, false, false, false, false, false];
        document.querySelectorAll('.nav-rail button').forEach(item => item.classList.remove('active'));
        nav('scene-editor');
        renderSceneEditor();
    }
    function backToScenes() { nav('scenes', document.querySelector('[data-page=scenes]')); }
    function readSceneEditor() {
        const scene = state.editing;
        if (!scene || !byId('scene-name')) return;
        scene.name = byId('scene-name').value.trim();
        scene.globalBrightness = Math.round(clamp(byId('scene-brightness').value,0,100) * 255 / 100);
        scene.transitionMs = clamp(byId('scene-fade').value,0,5000);

        let firstZ = state.targetZones ? state.targetZones.indexOf(true) : 0;
        if (firstZ === -1) firstZ = 0;

        const newLayers = scene.zones[firstZ].layers.map((layer, l) => {
            const newLayer = clone(layer);
            newLayer.effect = byId(`z-l${l}-effect`)?.value || layer.effect;
            newLayer.blendMode = byId(`z-l${l}-blend`)?.value || layer.blendMode;
            newLayer.paletteId = Number(byId(`z-l${l}-palette`)?.value || layer.paletteId || 0);
            const phex = byId(`z-l${l}-pcolor`)?.value || colorFor(layer.primaryColor);
            newLayer.primaryColor.r = parseInt(phex.slice(1,3),16); newLayer.primaryColor.g = parseInt(phex.slice(3,5),16); newLayer.primaryColor.b = parseInt(phex.slice(5,7),16);
            const shex = byId(`z-l${l}-scolor`)?.value || colorFor(layer.secondaryColor);
            newLayer.secondaryColor.r = parseInt(shex.slice(1,3),16); newLayer.secondaryColor.g = parseInt(shex.slice(3,5),16); newLayer.secondaryColor.b = parseInt(shex.slice(5,7),16);
            newLayer.speedMs = speedToMs(byId(`z-l${l}-speed`)?.value ?? msToSpeed(layer.speedMs));
            const meta = effect(newLayer.effect);
            if (meta.hasParameter) newLayer.fxParam = clamp(byId(`z-l${l}-param`)?.value ?? meta.parameterDefault, meta.parameterMin, meta.parameterMax);
            newLayer.opacity = Math.round(clamp(byId(`z-l${l}-opacity`)?.value ?? (layer.opacity * 100 / 255), 0, 100) * 255 / 100);
            newLayer.enabled = Boolean(byId(`z-l${l}-enabled`)?.checked);
            return newLayer;
        });

        scene.zones.forEach((zone, z) => {
            if (state.targetZones && state.targetZones[z]) {
                zone.layers = clone(newLayers);
            }
        });
    }
    function toggleTargetZone(z) {
        readSceneEditor();
        state.targetZones[z] = !state.targetZones[z];
        if (state.targetZones.indexOf(true) === -1) state.targetZones[z] = true;
        renderSceneEditor();
    }
    const palettes = [
        { id: 0, name: "Solid (Primary → Secondary)" },
        { id: 1, name: "Rainbow" },
        { id: 2, name: "Ocean" },
        { id: 3, name: "Lava" },
        { id: 4, name: "Forest" },
        { id: 5, name: "Sunset" }
    ];
    function renderSceneEditor() {
        const scene = state.editing;
        if (!scene) return;
        byId('scene-editor-title').textContent = scene.name || 'Untitled scene';
        byId('scene-name').value = scene.name || '';
        byId('scene-brightness').value = Math.round(scene.globalBrightness * 100 / 255);
        byId('scene-brightness-output').textContent = `${byId('scene-brightness').value}%`;
        byId('scene-fade').value = scene.transitionMs;
        byId('scene-fade-output').textContent = `${(scene.transitionMs / 1000).toFixed(1)} s`;

        let firstZ = state.targetZones ? state.targetZones.indexOf(true) : 0;
        if (firstZ === -1) { firstZ = 0; state.targetZones = [true, false, false, false, false, false, false, false]; }

        const zoneSelectors = `<div class="field"><label>Target Zones</label><div style="display:flex; gap:0.5rem; flex-wrap:wrap; margin-bottom:1rem;">
            ${[0,1,2,3,4,5,6,7].map(z => {
                const zName = state.config?.zones?.[z]?.name || `Zone ${z + 1}`;
                const isSelected = state.targetZones[z];
                return `<button class="${isSelected ? 'button-primary' : 'button-secondary'}" style="width:auto; margin:0;" onclick="lg.toggleTargetZone(${z})">${isSelected ? '✓ ' : ''}${esc(zName)}</button>`;
            }).join('')}
        </div></div>`;

        const layersHtml = scene.zones[firstZ].layers.map((layer, l) => {
            const meta = effect(layer.effect);
            const parameter = clamp(layer.fxParam || meta.parameterDefault, meta.parameterMin || 0, meta.parameterMax || 255);
            return `<div class="layer-editor">
                <h5>Layer ${l + 1}</h5>
                <div class="field"><label>Effect</label><select id="z-l${l}-effect" onchange="lg.changeZoneEffect(${l}, this.value)">
                    ${state.effects.map(item => `<option value="${esc(item.id)}" ${item.id === layer.effect ? 'selected' : ''}>${esc(item.label)}</option>`).join('')}
                </select></div>
                <div class="field"><label>Color Palette</label><select id="z-l${l}-palette">
                    ${palettes.map(p => `<option value="${p.id}" ${p.id === (layer.paletteId || 0) ? 'selected' : ''}>${esc(p.name)}</option>`).join('')}
                </select></div>
                <div class="field"><label>Blend Mode</label><select id="z-l${l}-blend">
                    <option value="normal" ${layer.blendMode==='normal'?'selected':''}>Normal</option>
                    <option value="add" ${layer.blendMode==='add'?'selected':''}>Add</option>
                    <option value="multiply" ${layer.blendMode==='multiply'?'selected':''}>Multiply</option>
                    <option value="screen" ${layer.blendMode==='screen'?'selected':''}>Screen</option>
                </select></div>
                <div class="color-line">
                    <label>Primary</label><input id="z-l${l}-pcolor" type="color" value="${colorFor(layer.primaryColor)}">
                    <label style="margin-left:1rem">Secondary</label><input id="z-l${l}-scolor" type="color" value="${colorFor(layer.secondaryColor)}">
                </div>
                <div class="range-row"><label>Opacity</label><input id="z-l${l}-opacity" type="range" min="0" max="100" value="${Math.round(layer.opacity * 100 / 255)}" oninput="lg.updateText('z-l${l}-opacity-out', this.value + '%')"><output id="z-l${l}-opacity-out">${Math.round(layer.opacity * 100 / 255)}%</output></div>
                <div class="range-row"><label>Speed</label><input id="z-l${l}-speed" type="range" min="1" max="100" value="${msToSpeed(layer.speedMs)}" oninput="lg.speedText('z-l${l}-speed-out', this.value)"><output id="z-l${l}-speed-out">${speedName(msToSpeed(layer.speedMs))}</output></div>
                ${meta.hasParameter ? `<div class="range-row"><label>${esc(meta.parameterLabel)}</label><input id="z-l${l}-param" type="range" min="${meta.parameterMin}" max="${meta.parameterMax}" value="${parameter}" oninput="lg.updateText('z-l${l}-param-out', this.value + ' ${esc(meta.parameterUnit)}')"><output id="z-l${l}-param-out">${parameter} ${esc(meta.parameterUnit)}</output></div>` : `<p class="help">${esc(meta.description)}</p>`}
                <div><label class="switch-row"><span>Layer Enabled</span><input id="z-l${l}-enabled" type="checkbox" ${layer.enabled ? 'checked' : ''}></label></div>
            </div>`;
        }).join('');

        byId('scene-zones').innerHTML = `<article class="product-card zone-card">${zoneSelectors}${layersHtml}</article>`;
    }
    function changeZoneEffect(l, value) {
        readSceneEditor();
        let firstZ = state.targetZones ? state.targetZones.indexOf(true) : 0;
        if (firstZ === -1) firstZ = 0;
        const newParam = effect(value).parameterDefault || 0;
        state.targetZones.forEach((isSelected, z) => {
            if (isSelected) {
                state.editing.zones[z].layers[l].effect = value;
                state.editing.zones[z].layers[l].fxParam = newParam;
            }
        });
        renderSceneEditor();
    }
    async function saveScene() {
        readSceneEditor();
        if (!state.editing.name) return toast('Give the scene a name before saving.', true);
        try {
            await api('/api/v1/presets', 'PUT', state.editing);
            await refresh();
            toast('Scene saved.');
            backToScenes();
        } catch (error) { toast(error.message, true); }
    }

    function renderTimeline() {
        if (!state.sequence) return;
        byId('timeline-bpm').value = clamp(state.sequence.bpm,40,240);
        byId('timeline-bpm-output').textContent = `${state.sequence.bpm} BPM`;
        byId('timeline-enabled').checked = state.sequence.enabled;
        byId('timeline-palette').innerHTML = state.presets.map(scene => `<button draggable="true" class="${state.selectedScene === scene.index ? 'selected' : ''}" ondragstart="lg.dragScene(event,${Number(scene.index)})" onclick="lg.selectPalette(${Number(scene.index)})"><strong>${esc(scene.name || 'Untitled scene')}</strong><span>${esc(effect(scene.zones[0].layers[0].effect).label)}</span></button>`).join('') || '<span class="help">Create a scene first, then place it on the timeline.</span>';
        byId('timeline-tracks').innerHTML = state.sequence.tracks.map((track,trackIndex) => `<div class="track"><div class="track-head"><strong>Zone ${trackIndex + 1}</strong><label class="help"><input type="checkbox" ${track.enabled ? 'checked' : ''} onchange="lg.toggleTrack(${trackIndex}, this.checked)"> Active</label><select onchange="lg.setLoop(${trackIndex}, this.value)" aria-label="Zone ${trackIndex + 1} loop length">${[1,2,4,8,12,16].map(length => `<option value="${length}" ${track.numSteps === length ? 'selected' : ''}>${length} steps</option>`).join('')}</select></div><div class="step-lane">${track.steps.map((step,stepIndex) => `<button class="step ${stepIndex >= track.numSteps ? 'inactive ' : ''}${state.selectedStep?.track === trackIndex && state.selectedStep?.step === stepIndex ? 'selected' : ''}" onclick="lg.selectStep(${trackIndex},${stepIndex})" ondragover="event.preventDefault()" ondrop="lg.dropScene(event,${trackIndex},${stepIndex})" title="Step ${stepIndex + 1}: ${esc(sceneName(step.presetIndex))}"><small>${stepIndex + 1}</small><span>${esc(sceneName(step.presetIndex))}</span></button>`).join('')}</div></div>`).join('');
        renderStepInspector();
    }
    function renderStepInspector() {
        const selected = state.selectedStep;
        const holder = byId('step-inspector');
        if (!selected) { holder.innerHTML = '<p class="help">Select a step to fine-tune its scene and hold length.</p>'; return; }
        const step = state.sequence.tracks[selected.track].steps[selected.step];
        holder.innerHTML = `<div class="product-card step-inspector"><div class="field"><label for="step-scene">Zone ${selected.track + 1}, step ${selected.step + 1}</label><select id="step-scene" onchange="lg.setStepScene(${selected.track},${selected.step},this.value)">${state.presets.map(scene => `<option value="${Number(scene.index)}" ${Number(scene.index) === Number(step.presetIndex) ? 'selected' : ''}>${esc(scene.name || 'Untitled scene')}</option>`).join('')}</select></div><div><p class="help">Hold for</p><div class="beat-options">${[1,2,4,8].map(beats => `<button class="${step.lengthBeats === beats ? 'active' : ''}" onclick="lg.setStepLength(${selected.track},${selected.step},${beats})">${beats} beat${beats === 1 ? '' : 's'}</button>`).join('')}</div></div></div>`;
    }
    function selectPalette(index) { state.selectedScene = Number(index); renderTimeline(); }
    function dragScene(event,index) { event.dataTransfer.setData('text/plain', String(index)); event.dataTransfer.effectAllowed = 'copy'; }
    function dropScene(event,track,step) { event.preventDefault(); const index = Number(event.dataTransfer.getData('text/plain')); if (Number.isFinite(index)) { state.sequence.tracks[track].steps[step].presetIndex = index; state.selectedStep = {track,step}; renderTimeline(); } }
    function selectStep(track,step) { if (state.selectedScene !== null) state.sequence.tracks[track].steps[step].presetIndex = state.selectedScene; state.selectedStep = {track,step}; renderTimeline(); }
    function setStepScene(track,step,index) { state.sequence.tracks[track].steps[step].presetIndex = Number(index); renderTimeline(); }
    function setStepLength(track,step,length) { state.sequence.tracks[track].steps[step].lengthBeats = Number(length); renderTimeline(); }
    function toggleTrack(track,enabled) { state.sequence.tracks[track].enabled = enabled; }
    function setLoop(track,length) { state.sequence.tracks[track].numSteps = Number(length); renderTimeline(); }
    function setBpm(value) { state.sequence.bpm = clamp(value,40,240); byId('timeline-bpm-output').textContent = `${state.sequence.bpm} BPM`; }
    function setTimelineEnabled(enabled) { state.sequence.enabled = enabled; }
    async function saveTimeline() {
        try { await api('/api/v1/sequence','PUT',state.sequence); toast('Timeline saved.'); }
        catch (error) { toast(error.message,true); }
    }

    function renderAutomation() {
        byId('cue-list').innerHTML = state.schedules.map((cue,index) => `<div class="cue"><label class="switch-row"><span>On</span><input type="checkbox" ${cue.enabled ? 'checked' : ''} onchange="lg.setCue(${index},'enabled',this.checked)"></label><input type="time" value="${String(cue.hour).padStart(2,'0')}:${String(cue.minute).padStart(2,'0')}" onchange="lg.setCueTime(${index},this.value)" aria-label="Cue time"><select onchange="lg.setCue(${index},'action',this.value)"><option value="preset" ${cue.action === 'preset' ? 'selected' : ''}>Activate scene</option><option value="sequence" ${cue.action === 'sequence' ? 'selected' : ''}>Start timeline</option></select><select ${cue.action === 'sequence' ? 'disabled' : ''} onchange="lg.setCue(${index},'presetIndex',this.value)" aria-label="Cue scene">${state.presets.map(scene => `<option value="${Number(scene.index)}" ${Number(cue.presetIndex) === Number(scene.index) ? 'selected' : ''}>${esc(scene.name || 'Untitled scene')}</option>`).join('')}</select><div class="days">${['S','M','T','W','T','F','S'].map((day,bit) => `<button class="${cue.daysOfWeek & (1 << bit) ? 'active' : ''}" title="${['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'][bit]}" onclick="lg.toggleDay(${index},${bit})">${day}</button>`).join('')}</div></div>`).join('');
    }
    function addCue() { const empty = state.schedules.findIndex(cue => !cue.enabled); if (empty < 0) return toast('All eight calendar cues are already in use.', true); state.schedules[empty] = {...state.schedules[empty],enabled:true}; renderAutomation(); }
    function setCue(index,key,value) { state.schedules[index][key] = key === 'presetIndex' ? Number(value) : value; renderAutomation(); }
    function setCueTime(index,value) { const [hour,minute] = value.split(':').map(Number); state.schedules[index].hour = clamp(hour,0,23); state.schedules[index].minute = clamp(minute,0,59); }
    function toggleDay(index,bit) { state.schedules[index].daysOfWeek ^= (1 << bit); renderAutomation(); }
    async function saveAutomation() {
        if (state.schedules.some(cue => cue.enabled && !cue.daysOfWeek)) return toast('Choose at least one day for every active cue.',true);
        try { await api('/api/v1/schedules','PUT',{entries:state.schedules}); toast('Automation saved.'); }
        catch (error) { toast(error.message,true); }
    }

    function renderSystem() {
        if (!state.config) return;
        byId('system-name').value = state.config.deviceName || '';
        byId('system-timezone').value = state.config.timezone || 'UTC0';
        byId('system-sync-enabled').checked = Boolean(state.config.syncEnabled);
        byId('system-sync-role').value = String(state.config.syncRole ?? 0);
        byId('system-mqtt-enabled').checked = Boolean(state.config.mqttEnabled);
        byId('system-mqtt-broker').value = state.config.mqttBroker || '';
        byId('system-mqtt-port').value = state.config.mqttPort || 1883;
        byId('system-fallback').innerHTML = state.presets.map(scene => `<option value="${Number(scene.index)}" ${Number(scene.index) === Number(state.config.fallbackPreset) ? 'selected' : ''}>${esc(scene.name || 'Untitled scene')}</option>`).join('');

        let physHtml = '';
        for (let i = 0; i < 16; ++i) {
            const p = state.config.physicalOutputs?.[i] || {type:0,hardwareId:i,gpioPin:0,enabled:false};
            physHtml += `<div class="hw-field">
                <div class="hw-field-head">
                    <span>Output ${i}</span>
                    <label class="switch-row" style="margin:0;padding:0"><input id="sys-phys-en-${i}" type="checkbox" ${p.enabled?'checked':''}></label>
                </div>
                <label>Type</label>
                <select id="sys-phys-type-${i}">
                    <option value="0" ${p.type===0?'selected':''}>RMT (Data)</option>
                    <option value="1" ${p.type===1?'selected':''}>PWM (Analog)</option>
                    <option value="2" ${p.type===2?'selected':''}>SPI</option>
                </select>
                <label>Hardware ID</label><input type="number" id="sys-phys-hw-${i}" value="${p.hardwareId}" min="0" max="15">
                <label>GPIO Pin</label><input type="number" id="sys-phys-pin-${i}" value="${p.gpioPin}" min="0" max="48">
            </div>`;
        }
        byId('system-outputs').innerHTML = physHtml;

        let fixHtml = '';
        for (let i = 0; i < 16; ++i) {
            const f = state.config.fixtures?.[i] || {profile:0,pixelOrder:1,physicalOutputIndex:[0,0,0,0],numPixels:0,maxBrightness:255,gammaCorrection:true,maxMilliwatts:0};
            fixHtml += `<div class="hw-field">
                <div class="hw-field-head">Fixture ${i}</div>
                <label>Profile</label>
                <select id="sys-fix-prof-${i}">
                    <option value="0" ${f.profile===0?'selected':''}>WS2812</option>
                    <option value="1" ${f.profile===1?'selected':''}>SK6812</option>
                    <option value="2" ${f.profile===2?'selected':''}>PWM Single</option>
                    <option value="3" ${f.profile===3?'selected':''}>PWM RGB</option>
                    <option value="4" ${f.profile===4?'selected':''}>PWM RGBW</option>
                </select>
                <label>Pixel Order</label>
                <select id="sys-fix-po-${i}">
                    <option value="0" ${f.pixelOrder===0?'selected':''}>RGB</option>
                    <option value="1" ${f.pixelOrder===1?'selected':''}>GRB</option>
                    <option value="2" ${f.pixelOrder===2?'selected':''}>RGBW</option>
                    <option value="3" ${f.pixelOrder===3?'selected':''}>GRBW</option>
                </select>
                <label>Length (Pixels)</label><input type="number" id="sys-fix-len-${i}" value="${f.numPixels}" min="0" max="2000">
                <label>Output Index</label><input type="number" id="sys-fix-out-${i}" value="${f.physicalOutputIndex[0]}" min="0" max="15">
            </div>`;
        }
        byId('system-fixtures').innerHTML = fixHtml;

        const roles = ['Unassigned', 'Sign', 'Sign Element', 'Ambiance Light', 'Main Light'];
        let zonesHtml = '';
        for (let i = 0; i < 8; ++i) {
            const z = state.config.zones?.[i] || {name: '', role: 'Unassigned', fixtureMask: 0};

            let fixChecks = '<div style="display:grid;grid-template-columns:repeat(4,1fr);gap:4px;margin-top:4px;">';
            for (let f = 0; f < 16; ++f) {
                fixChecks += `<label style="font-size:0.75rem"><input type="checkbox" id="sys-zmask-${i}-${f}" ${(z.fixtureMask & (1<<f))?'checked':''}> F${f}</label>`;
            }
            fixChecks += '</div>';

            zonesHtml += `<div class="hw-field">
                <div class="hw-field-head">Zone ${i+1}</div>
                <label>Name</label><input id="sys-zname-${i}" value="${esc(z.name)}" maxlength="23" placeholder="Zone ${i+1}">
                <label>Role</label><select id="sys-zrole-${i}">${roles.map(r => `<option value="${r}" ${z.role === r ? 'selected' : ''}>${r}</option>`).join('')}</select>
                <label>Fixtures in Zone</label>${fixChecks}
            </div>`;
        }
        byId('system-zones').innerHTML = zonesHtml;

        const nodes = [];
        if (state.sync) {
            nodes.push(`<article class="product-card node-card primary"><span class="scene-chip">● THIS NODE</span><h4>${esc(state.config.deviceName || 'Lighting node')}</h4><p class="muted">${esc(state.sync.activeRole)} · ${state.sync.clockLocked ? 'clock locked' : 'waiting for clock'}</p><p class="latency">${state.sync.clockJitterUs || 0} μs <span class="help">jitter</span></p></article>`);
            state.sync.peers.forEach(peer => nodes.push(`<article class="product-card node-card"><span class="scene-chip">● FOLLOWER</span><h4>${esc(peer.address)}</h4><p class="muted">Seen ${clockAge(peer.ageMs)}</p><p class="latency">${peer.latencyMs} ms</p></article>`));
        }
        byId('sync-topology').innerHTML = nodes.join('') || '<div class="empty-state">Sync is off or this node has not discovered any room peers yet.</div>';
    }
    async function saveSystem() {
        try {
            const config = {
                deviceName: byId('system-name').value.trim(),
                fallbackPreset: Number(byId('system-fallback').value),
                timezone: byId('system-timezone').value.trim() || 'UTC0',
                syncEnabled: byId('system-sync-enabled').checked,
                syncRole: Number(byId('system-sync-role').value),
                mqttEnabled: byId('system-mqtt-enabled').checked,
                mqttBroker: byId('system-mqtt-broker').value.trim(),
                mqttPort: Number(byId('system-mqtt-port').value),
                zones: [],
                physicalOutputs: [],
                fixtures: []
            };

            for (let i = 0; i < 16; ++i) {
                config.physicalOutputs.push({
                    type: Number(byId(`sys-phys-type-${i}`)?.value || 0),
                    hardwareId: Number(byId(`sys-phys-hw-${i}`)?.value || 0),
                    gpioPin: Number(byId(`sys-phys-pin-${i}`)?.value || 0),
                    enabled: byId(`sys-phys-en-${i}`)?.checked || false
                });

                const f = state.config.fixtures?.[i] || {whiteBalance:[255,255,255],maxBrightness:255,gammaCorrection:true,maxMilliwatts:0};
                config.fixtures.push({
                    profile: Number(byId(`sys-fix-prof-${i}`)?.value || 0),
                    pixelOrder: Number(byId(`sys-fix-po-${i}`)?.value || 0),
                    physicalOutputIndex: [Number(byId(`sys-fix-out-${i}`)?.value || 0),0,0,0],
                    numPixels: Number(byId(`sys-fix-len-${i}`)?.value || 0),
                    maxBrightness: f.maxBrightness,
                    whiteBalance: f.whiteBalance,
                    gammaCorrection: f.gammaCorrection,
                    maxMilliwatts: f.maxMilliwatts
                });
            }

            for (let i = 0; i < 8; ++i) {
                let fmask = 0;
                for (let f = 0; f < 16; ++f) {
                    if (byId(`sys-zmask-${i}-${f}`)?.checked) fmask |= (1 << f);
                }
                config.zones.push({
                    name: byId(`sys-zname-${i}`)?.value.trim() || '',
                    role: byId(`sys-zrole-${i}`)?.value || 'Unassigned',
                    fixtureMask: fmask
                });
            }
            await api('/api/v1/config','PUT',config);
            state.config = {...state.config,...config};
            toast('System settings saved. Restart the node to apply a sync role change.');
        } catch (error) { toast(error.message,true); }
    }
    async function refreshSync() {
        try { state.sync = await api('/api/v1/sync'); renderControl(); renderSystem(); }
        catch (error) { toast(error.message,true); }
    }
    function updateText(id,value) { const output = byId(id); if (output) output.textContent = value; }
    function speedText(id,value) { updateText(id,speedName(Number(value))); }
    function changeOverlay(value) {}
    function changeOverlayBeat(value) {}

    function toggleHwSection(id) {
        const el = byId(id);
        if (!el) return;
        el.classList.toggle('collapsed');
        const chevron = byId(id + '-chevron');
        if (chevron) chevron.textContent = el.classList.contains('collapsed') ? '▸' : '▾';
    }

    async function uploadOta() {
        const fileInput = byId('ota-file');
        const file = fileInput.files[0];
        if (!file) return toast('Please select a firmware binary first.', true);

        const statusEl = byId('ota-status');
        statusEl.textContent = 'Uploading firmware... please wait.';
        try {
            const response = await fetch('/api/v1/ota', {
                method: 'POST',
                headers: { 'Authorization': 'Bearer ' + state.token },
                body: file
            });
            if (!response.ok) throw new Error(`Upload failed: ${response.statusText}`);
            statusEl.textContent = 'Upload successful! Rebooting node...';
            toast('OTA successful. Node is restarting.');
            setTimeout(() => window.location.reload(), 5000);
        } catch(e) {
            statusEl.textContent = 'Error: ' + e.message;
            toast('OTA failed: ' + e.message, true);
        }
    }

    async function downloadBackup() {
        try {
            const response = await fetch('/api/v1/backup', {
                headers: { 'Authorization': 'Bearer ' + state.token }
            });
            if (!response.ok) throw new Error('Backup failed');
            const blob = await response.blob();
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.style.display = 'none';
            a.href = url;
            a.download = 'lumigrid_backup.json';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            toast('Configuration exported successfully.');
        } catch (e) { toast('Export failed: ' + e.message, true); }
    }

    async function uploadRestore() {
        const fileInput = byId('restore-file');
        const file = fileInput.files[0];
        if (!file) return toast('Please select a backup JSON file first.', true);
        try {
            const payload = await file.text();
            await api('/api/v1/restore', 'POST', JSON.parse(payload));
            toast('Restore successful. Node is restarting.');
            setTimeout(() => window.location.reload(), 3000);
        } catch (e) { toast('Restore failed: ' + e.message, true); }
    }

    async function loadCommissioning() {
        try {
            const health = await api('/api/v1/health');
            const stats = byId('health-stats');
            if (stats) {
                stats.textContent = `Uptime: ${health.uptime_s || 0}s\nFree Heap: ${health.free_heap || 0} B\nMin Free Heap: ${health.min_free_heap || 0} B`;
            }
            await loadLogs();
        } catch(e) { toast('Health check failed: ' + e.message, true); }
    }

    async function runTestSequence(type) {
        try {
            if (type === 'lane') {
                await api('/api/v1/factory/effect', 'POST', { effect: 'pixel_chase', durationMs: 5000 });
                toast('Running lane test sequence...');
            } else if (type === 'color') {
                await api('/api/v1/factory/effect', 'POST', { effect: 'rainbow', durationMs: 5000 });
                toast('Running color test sequence...');
            }
        } catch(e) { toast('Test sequence failed: ' + e.message, true); }
    }

    async function generateAcceptanceReport() {
        try {
            const health = await api('/api/v1/health');
            const status = await api('/api/v1/status');
            const report = {
                timestamp: new Date().toISOString(),
                health,
                status,
                commissioned: true
            };
            const blob = new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'acceptance_report.json';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            toast('Acceptance report generated.');
        } catch(e) { toast('Report generation failed: ' + e.message, true); }
    }

    async function loadLogs() {
        try {
            const response = await fetch('/api/v1/logs', {
                headers: { 'Authorization': 'Bearer ' + state.token }
            });
            if (!response.ok) throw new Error('Failed to fetch logs');
            const text = await response.text();
            const logsEl = byId('sys-logs');
            if (logsEl) logsEl.textContent = text || 'No logs recorded.';
        } catch(e) { toast('Failed to load logs: ' + e.message, true); }
    }

    return {connect,disconnect,refresh,nav,playScene,blackout,editScene,backToScenes,renderSceneEditor,changeZoneEffect,changeOverlay,changeOverlayBeat,saveScene,updateText,speedText,selectPalette,dragScene,dropScene,selectStep,setStepScene,setStepLength,toggleTrack,setLoop,setBpm,setTimelineEnabled,saveTimeline,addCue,setCue,setCueTime,toggleDay,saveAutomation,saveSystem,refreshSync,uploadOta,downloadBackup,uploadRestore,loadCommissioning,runTestSequence,generateAcceptanceReport,loadLogs,toggleTargetZone,toggleHwSection};
})();
</script>
</body>
</html>
)LGHTML";
