#pragma once

static const char dashboard_html[] = R"LGHTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Lumigrid OS</title>
    <style>
        :root {
            --bg: #0a0a0f;
            --surface: rgba(25, 25, 35, 0.7);
            --surface-hover: rgba(35, 35, 50, 0.9);
            --border: rgba(255, 255, 255, 0.1);
            --text: #f0f0f5;
            --text-muted: #9ba1a6;
            --primary: #6366f1;
            --primary-glow: rgba(99, 102, 241, 0.4);
            --success: #10b981;
            --danger: #ef4444;
            --radius: 12px;
            --font: 'Inter', system-ui, sans-serif;
        }

        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: var(--font);
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
            min-height: 100vh;
            background-image: 
                radial-gradient(circle at 15% 50%, rgba(99, 102, 241, 0.1), transparent 25%),
                radial-gradient(circle at 85% 30%, rgba(16, 185, 129, 0.1), transparent 25%);
        }

        .container { max-width: 1200px; margin: 0 auto; padding: 2rem; }
        
        /* Auth Screen */
        #auth {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100vh;
        }
        .auth-card {
            background: var(--surface);
            backdrop-filter: blur(16px);
            padding: 3rem;
            border-radius: var(--radius);
            border: 1px solid var(--border);
            text-align: center;
            width: 100%;
            max-width: 400px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.4);
        }

        h1, h2, h3 { margin-bottom: 1rem; font-weight: 600; }
        
        input, select, button {
            font-family: inherit;
            font-size: 1rem;
            border-radius: 8px;
            border: 1px solid var(--border);
            background: rgba(0,0,0,0.3);
            color: var(--text);
            padding: 0.75rem 1rem;
            width: 100%;
            margin-bottom: 1rem;
            transition: all 0.2s;
        }
        
        input:focus, select:focus {
            outline: none;
            border-color: var(--primary);
            box-shadow: 0 0 0 3px var(--primary-glow);
        }

        button {
            background: var(--surface);
            cursor: pointer;
            font-weight: 500;
        }
        button:hover { background: var(--surface-hover); }
        button.primary { background: var(--primary); border-color: var(--primary); }
        button.primary:hover { background: #4f46e5; box-shadow: 0 0 15px var(--primary-glow); }
        button.danger { background: var(--danger); border-color: var(--danger); }

        /* Dashboard Layout */
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 2rem;
            border-bottom: 1px solid var(--border);
            margin-bottom: 2rem;
        }
        
        .nav-tabs { display: flex; gap: 1rem; margin-bottom: 2rem; }
        .nav-tabs button { width: auto; margin: 0; background: transparent; border-color: transparent; }
        .nav-tabs button.active { background: var(--surface); border-color: var(--border); }

        .page { display: none; animation: fadeIn 0.3s; }
        .page.active { display: block; }
        @keyframes fadeIn { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: none; } }

        .card {
            background: var(--surface);
            backdrop-filter: blur(12px);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            padding: 1.5rem;
            margin-bottom: 1.5rem;
        }

        /* Preset Editor Grid */
        .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 1.5rem; }
        .grid-4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 1rem; }
        
        .channel-editor {
            background: rgba(0,0,0,0.2);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1rem;
        }

        /* Sequencer Grid */
        .seq-track {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            margin-bottom: 0.5rem;
            background: rgba(0,0,0,0.2);
            padding: 0.5rem;
            border-radius: 8px;
            overflow-x: auto;
        }
        .seq-step {
            min-width: 60px;
            height: 60px;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 6px;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            font-size: 0.75rem;
            cursor: pointer;
        }
        .seq-step.active {
            border-color: var(--primary);
            background: rgba(99, 102, 241, 0.2);
        }
        .seq-step input {
            width: 40px; padding: 2px; margin: 2px; text-align: center;
            font-size: 0.75rem; background: transparent; border: 1px solid transparent;
        }
        .seq-step input:focus { background: rgba(0,0,0,0.5); border-color: var(--primary); }

        .toast {
            position: fixed; bottom: 2rem; right: 2rem;
            background: var(--surface); padding: 1rem 2rem;
            border-radius: 8px; border-left: 4px solid var(--primary);
            box-shadow: 0 10px 25px rgba(0,0,0,0.5);
            transform: translateY(150%); transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
            z-index: 1000;
        }
        .toast.show { transform: translateY(0); }
        .toast.error { border-left-color: var(--danger); }
        
        /* Flex Utils */
        .flex { display: flex; gap: 1rem; }
        .items-center { align-items: center; }
        .justify-between { justify-content: space-between; }
        .mb-1 { margin-bottom: 1rem; }

        /* Product dashboard */
        #experience-auth, #product-app { min-height: 100vh; }
        #experience-auth[hidden], #product-app[hidden] { display: none; }
        #experience-auth {
            display: grid; place-items: center; padding: 1.5rem;
            background:
                radial-gradient(circle at 20% 15%, rgba(99,102,241,.28), transparent 32rem),
                radial-gradient(circle at 80% 80%, rgba(16,185,129,.18), transparent 28rem);
        }
        .experience-login {
            width: min(26rem, 100%); padding: 2rem; border: 1px solid var(--border);
            border-radius: 1.25rem; background: rgba(15,16,28,.88);
            box-shadow: 0 1.5rem 5rem rgba(0,0,0,.45);
        }
        .eyebrow { color: #a5b4fc; font-size: .72rem; font-weight: 800; letter-spacing: .15em; text-transform: uppercase; }
        .experience-login h1 { font-size: clamp(2rem, 8vw, 3rem); letter-spacing: -.06em; margin: .35rem 0 .65rem; }
        .muted { color: var(--text-muted); font-size: .9rem; }
        .experience-login label, .field label { display: block; font-size: .78rem; font-weight: 700; letter-spacing: .04em; margin: 1.2rem 0 .4rem; }
        #product-app { padding: 1.25rem; max-width: 90rem; margin: 0 auto; }
        #product-app button, #product-app input, #product-app select { width: auto; margin: 0; }
        #product-app input, #product-app select {
            min-height: 2.5rem; padding: .55rem .7rem; background: rgba(5,6,15,.55);
        }
        #product-app input[type="range"] { min-height: 0; padding: 0; accent-color: #818cf8; }
        #product-app input[type="checkbox"] { min-height: auto; accent-color: #818cf8; }
        .app-shell { display: grid; gap: 1.25rem; }
        .app-header {
            display: flex; gap: 1rem; align-items: center; justify-content: space-between;
            padding: 1rem 1.2rem; border: 1px solid var(--border); border-radius: 1rem;
            background: rgba(16,17,31,.78); backdrop-filter: blur(1rem);
        }
        .brand { display: flex; align-items: center; gap: .75rem; min-width: 0; }
        .brand-mark { width: 2.5rem; aspect-ratio: 1; display: grid; place-items: center; border-radius: .8rem; color: white; font-weight: 900; background: linear-gradient(135deg, #818cf8, #34d399); box-shadow: 0 .4rem 1.2rem rgba(99,102,241,.4); }
        .brand h2 { margin: 0; font-size: 1rem; letter-spacing: .08em; }
        .online-pill, .status-pill { display: inline-flex; align-items: center; gap: .4rem; font-size: .72rem; color: #bbf7d0; }
        .online-pill::before { content: ""; width: .45rem; height: .45rem; border-radius: 50%; background: #34d399; box-shadow: 0 0 .7rem #34d399; }
        .header-actions { display: flex; align-items: center; gap: .5rem; }
        .header-actions button, .button-secondary, .button-danger, .button-primary {
            border-radius: .65rem; padding: .65rem .85rem; border: 1px solid var(--border); color: var(--text); background: rgba(255,255,255,.06); cursor: pointer;
        }
        .button-primary { border-color: transparent; background: linear-gradient(135deg, #6366f1, #4f46e5); font-weight: 750; }
        .button-danger { color: #fecaca; border-color: rgba(248,113,113,.45); background: rgba(127,29,29,.32); }
        .header-actions button:hover, .button-secondary:hover { background: rgba(255,255,255,.13); }
        .button-primary:hover { filter: brightness(1.13); }
        .nav-rail { display: flex; flex-wrap: wrap; gap: .45rem; padding: .4rem; background: rgba(16,17,31,.62); border: 1px solid var(--border); border-radius: .9rem; }
        .nav-rail button { border: 0; background: transparent; color: var(--text-muted); border-radius: .6rem; padding: .65rem .9rem; cursor: pointer; }
        .nav-rail button.active { color: white; background: rgba(129,140,248,.2); box-shadow: inset 0 0 0 1px rgba(165,180,252,.25); }
        .product-page { display: none; animation: page-in .24s ease both; }
        .product-page.active { display: block; }
        @keyframes page-in { from { opacity: 0; transform: translateY(.5rem); } to { opacity: 1; transform: none; } }
        .page-heading { display: flex; justify-content: space-between; align-items: flex-end; gap: 1rem; margin: .5rem 0 1rem; flex-wrap: wrap; }
        .page-heading h3 { margin: 0; font-size: 1.3rem; letter-spacing: -.02em; }
        .overview-grid, .scene-grid, .zone-grid, .settings-grid, .sync-grid { display: grid; gap: 1rem; }
        .overview-grid { grid-template-columns: 1.1fr .9fr; }
        .scene-grid { grid-template-columns: repeat(auto-fill, minmax(14rem, 1fr)); }
        .zone-grid { grid-template-columns: repeat(auto-fill, minmax(16rem, 1fr)); }
        .settings-grid { grid-template-columns: repeat(auto-fit, minmax(17rem, 1fr)); }
        .sync-grid { grid-template-columns: repeat(auto-fit, minmax(12rem, 1fr)); }
        .product-card { padding: 1rem; border-radius: 1rem; border: 1px solid var(--border); background: rgba(20,21,37,.74); box-shadow: 0 .8rem 2rem rgba(0,0,0,.14); }
        .product-card h4 { margin: 0; font-size: .95rem; }
        .product-card p { margin: .4rem 0 0; }
        .scene-card { display: grid; gap: .85rem; min-height: 10rem; }
        .scene-card .scene-actions { display: flex; gap: .5rem; margin-top: auto; }
        .scene-card .scene-actions button { flex: 1; }
        .scene-chip { display: inline-flex; align-items: center; gap: .35rem; color: #c7d2fe; font-size: .73rem; font-weight: 700; }
        .stat-list { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: .65rem; margin-top: .85rem; }
        .stat-list div { padding: .65rem; border-radius: .65rem; background: rgba(0,0,0,.18); }
        .stat-list span { display: block; color: var(--text-muted); font-size: .68rem; text-transform: uppercase; letter-spacing: .07em; }
        .stat-list strong { font-size: .95rem; }
        .editor-toolbar, .timeline-toolbar, .cue-toolbar { display: flex; justify-content: space-between; align-items: center; gap: .75rem; flex-wrap: wrap; margin-bottom: 1rem; }
        .editor-toolbar > div, .timeline-toolbar > div, .cue-toolbar > div { display: flex; align-items: center; gap: .6rem; flex-wrap: wrap; }
        .range-row { display: grid; grid-template-columns: minmax(7rem, 1fr) minmax(8rem, 2fr) minmax(3.2rem, auto); gap: .65rem; align-items: center; margin-top: .8rem; }
        .range-row label { font-size: .78rem; font-weight: 700; }
        .range-row output { color: #c7d2fe; font-variant-numeric: tabular-nums; text-align: right; }
        .switch-row { display: flex; align-items: center; justify-content: space-between; gap: .8rem; padding: .55rem 0; font-size: .82rem; }
        .switch-row label { margin: 0; }
        .zone-card { display: grid; gap: .65rem; }
        .zone-title { display: flex; justify-content: space-between; align-items: center; }
        .zone-title span { color: var(--text-muted); font-size: .72rem; }
        .color-line { display: flex; justify-content: space-between; align-items: center; gap: .7rem; }
        .color-line input { width: 3.5rem !important; padding: .15rem !important; }
        .help { color: var(--text-muted); font-size: .72rem; line-height: 1.45; }
        .overlay-controls { display: grid; gap: .6rem; }
        .palette { display: flex; gap: .55rem; overflow-x: auto; padding-bottom: .3rem; }
        .palette button { min-width: 8.5rem; text-align: left; border-radius: .7rem; padding: .65rem; border: 1px solid var(--border); background: rgba(255,255,255,.04); color: var(--text); cursor: grab; }
        .palette button.selected { border-color: #818cf8; background: rgba(99,102,241,.2); }
        .track { display: grid; grid-template-columns: 9rem minmax(36rem, 1fr); gap: .65rem; align-items: center; margin: .55rem 0; }
        .track-head { display: grid; gap: .35rem; padding: .55rem; border-radius: .65rem; background: rgba(255,255,255,.04); }
        .track-head strong { font-size: .78rem; }
        .track-head select { width: 100% !important; min-height: 2rem !important; font-size: .72rem; }
        .step-lane { display: grid; grid-template-columns: repeat(16, minmax(3.4rem, 1fr)); gap: .3rem; }
        .step { min-height: 3.5rem; overflow: hidden; border-radius: .5rem; border: 1px solid rgba(255,255,255,.1); background: rgba(255,255,255,.035); color: var(--text-muted); cursor: pointer; padding: .3rem; text-align: left; }
        .step span { display: block; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: .63rem; }
        .step small { color: #a5b4fc; font-size: .57rem; }
        .step.selected { border-color: #818cf8; box-shadow: 0 0 0 2px rgba(129,140,248,.2); }
        .step.inactive { opacity: .34; }
        .timeline-scroll { overflow-x: auto; padding-bottom: .5rem; }
        .step-inspector { margin-top: 1rem; display: grid; grid-template-columns: 1fr auto; gap: 1rem; align-items: end; }
        .beat-options { display: flex; gap: .35rem; }
        .beat-options button { padding: .45rem .6rem; border: 1px solid var(--border); border-radius: .45rem; color: var(--text-muted); background: transparent; cursor: pointer; }
        .beat-options button.active { color: white; background: rgba(99,102,241,.25); border-color: #818cf8; }
        .cue-list { display: grid; gap: .65rem; }
        .cue { display: grid; grid-template-columns: auto 7rem 1fr 1.3fr auto; align-items: center; gap: .65rem; padding: .75rem; border: 1px solid var(--border); border-radius: .75rem; background: rgba(255,255,255,.035); }
        .days { display: flex; gap: .2rem; }
        .days button { border: 1px solid var(--border); border-radius: .35rem; background: transparent; color: var(--text-muted); padding: .3rem .38rem; font-size: .65rem; cursor: pointer; }
        .days button.active { color: white; background: #4f46e5; border-color: #6366f1; }
        .node-card { min-height: 7.5rem; }
        .node-card.primary { border-color: rgba(52,211,153,.5); background: linear-gradient(145deg, rgba(16,185,129,.14), rgba(20,21,37,.74)); }
        .node-card .latency { color: #a7f3d0; font-size: 1.2rem; font-weight: 800; margin-top: .7rem; }
        .empty-state { padding: 2rem; text-align: center; color: var(--text-muted); border: 1px dashed var(--border); border-radius: .9rem; }
        #experience-toast { position: fixed; z-index: 30; right: 1rem; bottom: 1rem; max-width: min(28rem, calc(100vw - 2rem)); padding: .8rem 1rem; border-radius: .7rem; background: #1e293b; border: 1px solid #475569; color: white; transform: translateY(8rem); transition: transform .22s ease; }
        #experience-toast.show { transform: translateY(0); }
        #experience-toast.error { background: #450a0a; border-color: #f87171; }
        @media (max-width: 52rem) {
            #product-app { padding: .75rem; }
            .overview-grid { grid-template-columns: 1fr; }
            .app-header { align-items: flex-start; }
            .track { grid-template-columns: 1fr; }
            .cue { grid-template-columns: 1fr 1fr; }
            .cue .days { grid-column: 1 / -1; }
            .step-inspector { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
<template id="legacy-dashboard">

<div id="auth">
    <div class="auth-card">
        <h1>LUMIGRID OS</h1>
        <p class="text-muted mb-1">Enter access token</p>
        <input type="password" id="token" placeholder="Bearer Token">
        <button class="primary" onclick="connect()">Connect to Node</button>
    </div>
</div>

<div id="app" class="container" style="display:none;">
    <header>
        <div>
            <h2>LUMIGRID <span id="node-name" style="font-weight:300;color:var(--primary)">NODE</span></h2>
            <div style="font-size:0.875rem;color:var(--success)">● Online</div>
        </div>
        <button style="width:auto;margin:0" onclick="disconnect()">Disconnect</button>
    </header>

        <div class="nav-tabs">
        <button class="active" onclick="nav('overview')">Overview</button>
        <button onclick="nav('presets'); loadPresets()">Presets</button>
        <button onclick="nav('sequencer'); loadSequencer()">Sequencer</button>
        <button onclick="nav('commissioning'); loadCommissioning()">Commissioning</button>
        <button onclick="nav('settings'); loadSettings()">Settings</button>
        <button onclick="nav('system')">System</button>
    </div>

    <!-- OVERVIEW -->
    <div id="page-overview" class="page active">
        <div class="grid-2">
            <div class="card">
                <h3>Quick Actions</h3>
                <button class="danger" onclick="api('/api/v1/factory/abort', 'POST')">Emergency Blackout</button>
                <button onclick="api('/api/v1/activate', 'POST', {preset: 0})">Load Default Preset</button>
            </div>
            <div class="card">
                <h3>System Status</h3>
                <pre id="status-json" style="font-size:0.8rem;overflow:auto;background:rgba(0,0,0,0.3);padding:1rem;border-radius:8px;"></pre>
                <button class="mb-1" onclick="loadStatus()">Refresh Status</button>
            </div>
        </div>
    </div>

    
    <!-- COMMISSIONING -->
    <div id="page-commissioning" class="page">
        <div class="flex justify-between items-center mb-1">
            <h3>Commissioning Wizard</h3>
            <button style="width:auto;margin:0" onclick="generateAcceptanceReport()">Generate Report</button>
        </div>
        
        <div class="grid-2">
            <div class="card">
                <h4>1. Network Health</h4>
                <div id="health-stats" style="margin: 1rem 0; font-family: monospace;">Loading...</div>
                <button onclick="loadCommissioning()">Refresh Health</button>
            </div>
            
            <div class="card">
                <h4>2. Wiring & Lane Test</h4>
                <p class="muted">Runs diagnostic sequence across all lanes to verify data integrity.</p>
                <div class="flex" style="margin-top:1rem;">
                    <button onclick="runTestSequence('lane')">Test Lanes</button>
                    <button onclick="runTestSequence('color')">Color Order Test</button>
                </div>
            </div>
            
            <div class="card" style="grid-column: 1 / -1;">
                <h4>3. System Logs</h4>
                <pre id="sys-logs" style="height:200px; overflow-y:auto; background:rgba(0,0,0,0.3); padding:1rem; border-radius:8px; font-size:0.8rem;"></pre>
                <button onclick="loadLogs()" style="margin-top:1rem;">Refresh Logs</button>
            </div>
        </div>
    </div>

    <!-- PRESETS -->
    <div id="page-presets" class="page">
        <div class="flex justify-between items-center mb-1">
            <h3>Brand Presets</h3>
            <button style="width:auto;margin:0" onclick="loadPresets()">Refresh</button>
        </div>
        <div id="preset-list" class="grid-2"></div>
    </div>

    <!-- PRESET EDITOR MODAL (Inline Page) -->
    <div id="page-preset-edit" class="page">
        <div class="flex justify-between items-center mb-1">
            <h3 id="edit-preset-title">Edit Preset</h3>
            <div>
                <button style="width:auto;margin:0" onclick="nav('presets')">Cancel</button>
                <button class="primary" style="width:auto;margin:0" onclick="savePreset()">Save Changes</button>
            </div>
        </div>
        
        <div class="card grid-2">
            <div>
                <label>Preset Name</label>
                <input type="text" id="pe-name">
            </div>
            <div>
                <label>Global Brightness (0-255)</label>
                <input type="number" id="pe-brightness" min="0" max="255">
            </div>
            <div>
                <label>Crossfade Transition (ms)</label>
                <input type="number" id="pe-transition" min="0" max="10000">
            </div>
        </div>

        <div id="pe-channels" class="grid-4">
            <!-- Populated via JS -->
        </div>
    </div>

    <!-- SEQUENCER -->
    <div id="page-sequencer" class="page">
        <div class="card flex justify-between items-center">
            <div class="flex items-center">
                <label style="margin-right:1rem">Global BPM</label>
                <input type="number" id="seq-bpm" style="width:100px;margin:0">
            </div>
            <div class="flex items-center">
                <input type="checkbox" id="seq-enabled" style="width:auto;margin:0;margin-right:0.5rem">
                <label>Sequencer Enabled</label>
            </div>
            <button class="primary" style="width:auto;margin:0" onclick="saveSequencer()">Save Sequencer</button>
        </div>
        <div class="card">
            <h3>16-Step Tracks (Per Channel)</h3>
            <p class="text-muted mb-1">Enter the Preset Index (0-31) and Length in Beats. Click a step to enable/disable it.</p>
            <div id="seq-tracks"></div>
        </div>
    </div>

    <!-- SETTINGS -->
    <div id="page-settings" class="page">
        <div class="card grid-2">
            <div>
                <label>Device Name</label><input type="text" id="cfg-name">
                <label>Active Preset Index</label><input type="number" id="cfg-active">
                <label>Fallback Preset Index</label><input type="number" id="cfg-fallback">
                <label>Timezone String</label><input type="text" id="cfg-timezone">
            </div>
            <div>
                <label>Max Power (mW, 0=off)</label><input type="number" id="cfg-power">
                <label>Watchdog Timeout (s)</label><input type="number" id="cfg-watchdog">
                <label>Soft Start (ms)</label><input type="number" id="cfg-softstart">
                <label>Sync Role (0=Auto, 1=Master, 2=Worker)</label><input type="number" id="cfg-role">
                <div class="flex items-center mt-1">
                    <input type="checkbox" id="cfg-softstart-enabled" style="width:auto"><label>Soft Start Enabled</label>
                </div>
                <div class="flex items-center">
                    <input type="checkbox" id="cfg-sync-enabled" style="width:auto"><label>Sync Enabled</label>
                </div>
            </div>
        </div>
        <div class="card mt-2">
            <h4>Security</h4>
            <div style="display:flex;gap:1rem;margin-top:0.5rem">
                <input type="password" id="cfg-new-token" placeholder="New API Token (min 4 chars)">
                <button class="primary" onclick="updateAuthToken()">Update Token</button>
            </div>
        </div>
        <button class="primary mt-2" onclick="saveSettings()">Save Settings</button>
    </div>
</div>

<div id="toast" class="toast">Message</div>

<script>
const $ = id => document.getElementById(id);
let token = '', currentPresetEditIndex = 0, currentPresetData = null;

const toast = (msg, err=false) => {
    const e = $('toast');
    e.textContent = msg;
    e.className = 'toast show' + (err ? ' error' : '');
    setTimeout(() => e.className = 'toast', 3000);
};

async function api(path, method='GET', body) {
    const res = await fetch(path, {
        method,
        headers: {
            'Authorization': 'Bearer ' + token,
            'Content-Type': 'application/json'
        },
        body: body ? JSON.stringify(body) : undefined
    });
    if(!res.ok) {
        let m = `HTTP ${res.status}`;
        try { m = (await res.json()).error || m; } catch(e){}
        throw Error(m);
    }
    return res.json();
}

async function connect() {
    token = $('token').value;
    try {
        await loadStatus();
        $('auth').style.display = 'none';
        $('app').style.display = 'block';
        toast('Connected successfully');
    } catch(e) {
        toast('Connection failed: ' + e.message, true);
    }
}

function disconnect() {
    token = '';
    $('app').style.display = 'none';
    $('auth').style.display = 'flex';
}

function nav(page) {
    document.querySelectorAll('.nav-tabs button').forEach(b => b.classList.remove('active'));
    event.target.classList.add('active');
    document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
    $('page-' + page).classList.add('active');
}

// Overview
async function loadStatus() {
    try {
        const [fac, sig] = await Promise.all([
            api('/api/v1/factory/status'),
            api('/api/v1/status')
        ]);
        $('status-json').textContent = JSON.stringify({factory: fac, signage: sig}, null, 2);
    } catch(e) { toast(e.message, true); }
}

async function updateAuthToken() {
    const newToken = $('cfg-new-token').value;
    if (newToken.length < 4) return toast('Token must be at least 4 characters', true);
    
    try {
        await api('/api/v1/auth/update', 'POST', { newToken });
        toast('Token updated successfully. Re-authenticating...');
        token = newToken; // Use new token immediately
        $('cfg-new-token').value = '';
    } catch(e) { toast(e.message, true); }
}

// Presets
async function loadPresets() {
    try {
        const presets = await api('/api/v1/presets');
        const list = $('preset-list');
        list.innerHTML = '';
        presets.forEach(p => {
            const el = document.createElement('div');
            el.className = 'card';
            el.innerHTML = `
                <div class="flex justify-between items-center">
                    <div>
                        <h4 style="color:var(--primary)">[${p.index}] ${p.name || 'Untitled'}</h4>
                        <div style="font-size:0.8rem;color:var(--text-muted)">Brightness: ${p.brightness}</div>
                    </div>
                    <div>
                        <button style="padding:0.5rem;font-size:0.8rem" onclick="api('/api/v1/activate','POST',{preset:${p.index}}).then(()=>toast('Activated'))">Play</button>
                        <button style="padding:0.5rem;font-size:0.8rem" onclick="openPresetEditor(${p.index})">Edit</button>
                    </div>
                </div>
            `;
            list.appendChild(el);
        });
    } catch(e) { toast(e.message, true); }
}

async function openPresetEditor(index) {
    currentPresetEditIndex = index;
    try {
        const presets = await api('/api/v1/presets');
        currentPresetData = presets.find(p => p.index === index);
        if(!currentPresetData) throw Error("Preset not found");
        
        $('edit-preset-title').textContent = `Edit Preset [${index}]`;
        $('pe-name').value = currentPresetData.name;
        $('pe-brightness').value = currentPresetData.brightness;
        $('pe-transition').value = currentPresetData.transitionMs;

        const chCont = $('pe-channels');
        chCont.innerHTML = '';
        const fxOpts = ['solid','breathe','chase','sweep','reveal','sparkle','gradient','strobe','fire'];
        
        currentPresetData.channels.forEach((c, i) => {
            const hex = '#' + [c.r, c.g, c.b].map(x => x.toString(16).padStart(2,'0')).join('');
            chCont.innerHTML += `
                <div class="channel-editor">
                    <h4>CH ${i}</h4>
                    <label>Effect</label>
                    <select id="pe-fx-${i}">${fxOpts.map(f => `<option value="${f}" ${c.effect===f?'selected':''}>${f}</option>`).join('')}</select>
                    <label>Color</label>
                    <input type="color" id="pe-col-${i}" value="${hex}">
                    <label>PWM (0-4095)</label>
                    <input type="number" id="pe-pwm-${i}" value="${c.pwmDuty}">
                    <label>Speed (ms)</label>
                    <input type="number" id="pe-spd-${i}" value="${c.effectSpeedMs}">
                    <label>Offset</label>
                    <input type="number" id="pe-off-${i}" value="${c.phaseOffset}">
                    <div style="display:flex;gap:1rem;margin-top:0.5rem;">
                        <label><input type="checkbox" id="pe-en-px-${i}" ${c.pixelEnabled?'checked':''}> Pixels</label>
                        <label><input type="checkbox" id="pe-en-pwm-${i}" ${c.pwmEnabled?'checked':''}> PWM</label>
                    </div>
                </div>
            `;
        });
        nav('preset-edit');
    } catch(e) { toast(e.message, true); }
}

async function savePreset() {
    const payload = {
        index: currentPresetEditIndex,
        name: $('pe-name').value,
        brightness: Number($('pe-brightness').value),
        transitionMs: Number($('pe-transition').value),
        channels: []
    };
    for(let i=0; i<8; i++) {
        const hex = $(`pe-col-${i}`).value;
        payload.channels.push({
            effect: $(`pe-fx-${i}`).value,
            r: parseInt(hex.slice(1,3), 16),
            g: parseInt(hex.slice(3,5), 16),
            b: parseInt(hex.slice(5,7), 16),
            pwmDuty: Number($(`pe-pwm-${i}`).value),
            effectSpeedMs: Number($(`pe-spd-${i}`).value),
            phaseOffset: Number($(`pe-off-${i}`).value),
            paletteId: 0, fxParam: 0,
            pixelEnabled: $(`pe-en-px-${i}`).checked,
            pwmEnabled: $(`pe-en-pwm-${i}`).checked
        });
    }
    try {
        await api('/api/v1/presets', 'PUT', payload);
        toast('Preset saved');
        nav('presets');
        loadPresets();
    } catch(e) { toast(e.message, true); }
}

// Sequencer
async function loadSequencer() {
    try {
        const seq = await api('/api/v1/sequence');
        $('seq-bpm').value = seq.bpm;
        $('seq-enabled').checked = seq.enabled;
        const cont = $('seq-tracks');
        cont.innerHTML = '';
        seq.tracks.forEach((t, i) => {
            let stepsHtml = '';
            for(let s=0; s<16; s++) {
                const step = t.steps[s] || {presetIndex:0, lengthBeats:1};
                const isActive = s < t.numSteps;
                stepsHtml += `
                    <div class="seq-step ${isActive?'active':''}" onclick="this.classList.toggle('active'); updateNumSteps(${i})">
                        <input type="number" id="sq-p-${i}-${s}" value="${step.presetIndex}" title="Preset Index" onclick="event.stopPropagation()">
                        <input type="number" id="sq-l-${i}-${s}" value="${step.lengthBeats}" title="Beats Length" onclick="event.stopPropagation()">
                    </div>
                `;
            }
            cont.innerHTML += `
                <div class="seq-track" id="track-${i}" data-steps="${t.numSteps}">
                    <div style="width:100px;">
                        <label><input type="checkbox" id="sq-en-${i}" ${t.enabled?'checked':''}> TRK ${i}</label>
                    </div>
                    ${stepsHtml}
                </div>
            `;
        });
    } catch(e) { toast(e.message, true); }
}

function updateNumSteps(trackIdx) {
    const track = $(`track-${trackIdx}`);
    const steps = track.querySelectorAll('.seq-step');
    let count = 0;
    steps.forEach(s => { if(s.classList.contains('active')) count++; });
    track.dataset.steps = count || 1; // min 1
}

async function saveSequencer() {
    const seq = {
        bpm: Number($('seq-bpm').value),
        enabled: $('seq-enabled').checked,
        tracks: []
    };
    for(let i=0; i<8; i++) {
        const trk = {
            channelId: i,
            enabled: $(`sq-en-${i}`).checked,
            numSteps: Number($(`track-${i}`).dataset.steps) || 1,
            steps: []
        };
        for(let s=0; s<16; s++) {
            trk.steps.push({
                presetIndex: Number($(`sq-p-${i}-${s}`).value),
                lengthBeats: Number($(`sq-l-${i}-${s}`).value)
            });
        }
        seq.tracks.push(trk);
    }
    try {
        await api('/api/v1/sequence', 'PUT', seq);
        toast('Sequence saved');
    } catch(e) { toast(e.message, true); }
}

// Settings
async function loadSettings() {
    try {
        const c = await api('/api/v1/config');
        $('cfg-name').value = c.deviceName;
        $('cfg-active').value = c.activePreset;
        $('cfg-fallback').value = c.fallbackPreset;
        $('cfg-power').value = c.maxPowerMw;
        $('cfg-watchdog').value = c.watchdogTimeoutS;
        $('cfg-softstart').value = c.softStartMs;
        $('cfg-timezone').value = c.timezone;
        $('cfg-role').value = c.syncRole;
        $('cfg-softstart-enabled').checked = c.softStart;
        $('cfg-sync-enabled').checked = c.syncEnabled;
    } catch(e) { toast(e.message, true); }
}

async function saveSettings() {
    try {
        await api('/api/v1/config', 'PUT', {
            deviceName: $('cfg-name').value,
            activePreset: Number($('cfg-active').value),
            fallbackPreset: Number($('cfg-fallback').value),
            maxPowerMw: Number($('cfg-power').value),
            watchdogTimeoutS: Number($('cfg-watchdog').value),
            softStartMs: Number($('cfg-softstart').value),
            timezone: $('cfg-timezone').value,
            syncRole: Number($('cfg-role').value),
            softStart: $('cfg-softstart-enabled').checked,
            syncEnabled: $('cfg-sync-enabled').checked
        });
        toast('Settings saved');
    } catch(e) { toast(e.message, true); }
}
</script>
</template>

<section id="experience-auth" aria-labelledby="login-title">
    <div class="experience-login">
        <form onsubmit="lg.connect(); return false;">
        <p class="eyebrow">LumiGrid lighting control</p>
        <h1 id="login-title">Make light feel alive.</h1>
        <p class="muted">Connect securely to this node to arrange scenes, timelines, and automation.</p>
        <input type="text" autocomplete="username" tabindex="-1" aria-hidden="true" hidden>
        <label for="experience-token">Node access token</label>
        <input id="experience-token" type="password" autocomplete="current-password" placeholder="Enter the token supplied with this node">
        <button class="button-primary" type="submit" style="width:100%;margin-top:1rem">Connect</button>
        </form>
    </div>
</section>

<main id="product-app" hidden>
    <div class="app-shell">
        <header class="app-header">
            <div class="brand">
                <div class="brand-mark" aria-hidden="true">LG</div>
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
        </header>

        <nav class="nav-rail" aria-label="Lighting controls">
            <button class="active" data-page="control" onclick="lg.nav('control', this)">Control</button>
            <button data-page="scenes" onclick="lg.nav('scenes', this)">Scenes</button>
            <button data-page="timeline" onclick="lg.nav('timeline', this)">Timeline</button>
            <button data-page="automation" onclick="lg.nav('automation', this)">Automation</button>
            <button data-page="system" onclick="lg.nav('system', this)">System</button>
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
                        <div class="range-row">
                            <label for="scene-brightness">Room brightness</label>
                            <input id="scene-brightness" type="range" min="0" max="100" oninput="lg.updateText('scene-brightness-output', this.value + '%')">
                            <output id="scene-brightness-output"></output>
                        </div>
                        <div class="range-row">
                            <label for="scene-fade">Scene fade</label>
                            <input id="scene-fade" type="range" min="0" max="5000" step="100" oninput="lg.updateText('scene-fade-output', (this.value / 1000).toFixed(1) + ' s')">
                            <output id="scene-fade-output"></output>
                        </div>
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
                <div><p class="eyebrow">Performance timeline</p><h3>Build the room’s rhythm</h3></div>
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
                    <h4>Physical Zones</h4>
                    <p class="help">Map your 8 physical outputs to room features.</p>
                    <div id="system-zones" class="zone-grid" style="margin-top:1rem"></div>
                </article>
            </div>
            <div class="page-heading" style="margin-top:1.5rem"><div><p class="eyebrow">Topology</p><h3>Nodes in this room</h3></div><button class="button-secondary" onclick="lg.refreshSync()">Refresh health</button></div>
            <div id="sync-topology" class="sync-grid"></div>
            
            <div class="page-heading" style="margin-top:2.5rem"><div><p class="eyebrow">Maintenance</p><h3>Firmware & Config</h3></div></div>
            <div class="settings-grid">
                <article class="product-card">
                    <h4>Over-The-Air Update (OTA)</h4>
                    <p class="help mb-1">Upload a signed firmware binary to update the node.</p>
                    <input type="file" id="ota-file" accept=".bin" style="width: 100%; margin-bottom: 1rem;">
                    <button class="button-primary" style="width: 100%" onclick="lg.uploadOta()">Update Firmware</button>
                    <div id="ota-status" class="help" style="margin-top: 1rem;"></div>
                </article>
                <article class="product-card">
                    <h4>Configuration Export & Import</h4>
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

    async function connect() {
        state.token = byId('experience-token').value.trim();
        if (!state.token) return toast('Enter the node access token first.', true);
        try {
            await refresh();
            byId('experience-auth').hidden = true;
            byId('product-app').hidden = false;
            nav('control', document.querySelector('[data-page=control]'));
            toast('Connected to the node.');
        } catch (error) {
            toast(`Could not connect: ${error.message}`, true);
        }
    }
    function disconnect() {
        state.token = '';
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
        return `<article class="product-card scene-card"><div><span class="scene-chip">● ${esc(firstEffect.label)}</span><h4>${esc(scene.name || 'Untitled scene')}</h4><p class="muted">${Math.round(scene.globalBrightness * 100 / 255)}% brightness · ${Math.round(scene.transitionMs / 100) / 10}s fade</p></div><p class="help">${esc(firstEffect.description)}</p><div class="scene-actions"><button class="button-secondary" onclick="lg.playScene(${Number(scene.index)})">Play</button><button class="button-secondary" onclick="lg.editScene(${Number(scene.index)})">Edit</button></div></article>`;
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
        scene.zones.forEach((zone, z) => {
            zone.layers.forEach((layer, l) => {
                layer.effect = byId(`z${z}-l${l}-effect`)?.value || layer.effect;
                layer.blendMode = byId(`z${z}-l${l}-blend`)?.value || layer.blendMode;
                const phex = byId(`z${z}-l${l}-pcolor`)?.value || colorFor(layer.primaryColor);
                layer.primaryColor.r = parseInt(phex.slice(1,3),16); layer.primaryColor.g = parseInt(phex.slice(3,5),16); layer.primaryColor.b = parseInt(phex.slice(5,7),16);
                const shex = byId(`z${z}-l${l}-scolor`)?.value || colorFor(layer.secondaryColor);
                layer.secondaryColor.r = parseInt(shex.slice(1,3),16); layer.secondaryColor.g = parseInt(shex.slice(3,5),16); layer.secondaryColor.b = parseInt(shex.slice(5,7),16);
                layer.speedMs = speedToMs(byId(`z${z}-l${l}-speed`)?.value ?? msToSpeed(layer.speedMs));
                const meta = effect(layer.effect);
                if (meta.hasParameter) layer.fxParam = clamp(byId(`z${z}-l${l}-param`)?.value ?? meta.parameterDefault, meta.parameterMin, meta.parameterMax);
                layer.opacity = Math.round(clamp(byId(`z${z}-l${l}-opacity`)?.value ?? (layer.opacity * 100 / 255), 0, 100) * 255 / 100);
                layer.enabled = Boolean(byId(`z${z}-l${l}-enabled`)?.checked);
            });
        });
    }
    function renderSceneEditor() {
        const scene = state.editing;
        if (!scene) return;
        byId('scene-editor-title').textContent = scene.name || 'Untitled scene';
        byId('scene-name').value = scene.name || '';
        byId('scene-brightness').value = Math.round(scene.globalBrightness * 100 / 255);
        byId('scene-brightness-output').textContent = `${byId('scene-brightness').value}%`;
        byId('scene-fade').value = scene.transitionMs;
        byId('scene-fade-output').textContent = `${(scene.transitionMs / 1000).toFixed(1)} s`;
        byId('scene-zones').innerHTML = scene.zones.map((zone, z) => {
            const zName = state.config?.zones?.[z]?.name || `Zone ${z + 1}`;
            const layersHtml = zone.layers.map((layer, l) => {
                const meta = effect(layer.effect);
                const parameter = clamp(layer.fxParam || meta.parameterDefault, meta.parameterMin || 0, meta.parameterMax || 255);
                return `<div class="layer-editor" style="margin-top:1rem; border-top:1px solid var(--border); padding-top:1rem;">
                    <h5>Layer ${l + 1}</h5>
                    <div class="field"><label>Effect</label><select id="z${z}-l${l}-effect" onchange="lg.changeZoneEffect(${z}, ${l}, this.value)">
                        ${state.effects.map(item => `<option value="${esc(item.id)}" ${item.id === layer.effect ? 'selected' : ''}>${esc(item.label)}</option>`).join('')}
                    </select></div>
                    <div class="field"><label>Blend Mode</label><select id="z${z}-l${l}-blend">
                        <option value="normal" ${layer.blendMode==='normal'?'selected':''}>Normal</option>
                        <option value="add" ${layer.blendMode==='add'?'selected':''}>Add</option>
                        <option value="multiply" ${layer.blendMode==='multiply'?'selected':''}>Multiply</option>
                        <option value="screen" ${layer.blendMode==='screen'?'selected':''}>Screen</option>
                    </select></div>
                    <div class="color-line">
                        <label>Primary</label><input id="z${z}-l${l}-pcolor" type="color" value="${colorFor(layer.primaryColor)}">
                        <label style="margin-left:1rem">Secondary</label><input id="z${z}-l${l}-scolor" type="color" value="${colorFor(layer.secondaryColor)}">
                    </div>
                    <div class="range-row"><label>Opacity</label><input id="z${z}-l${l}-opacity" type="range" min="0" max="100" value="${Math.round(layer.opacity * 100 / 255)}" oninput="lg.updateText('z${z}-l${l}-opacity-out', this.value + '%')"><output id="z${z}-l${l}-opacity-out">${Math.round(layer.opacity * 100 / 255)}%</output></div>
                    <div class="range-row"><label>Speed</label><input id="z${z}-l${l}-speed" type="range" min="1" max="100" value="${msToSpeed(layer.speedMs)}" oninput="lg.speedText('z${z}-l${l}-speed-out', this.value)"><output id="z${z}-l${l}-speed-out">${speedName(msToSpeed(layer.speedMs))}</output></div>
                    ${meta.hasParameter ? `<div class="range-row"><label>${esc(meta.parameterLabel)}</label><input id="z${z}-l${l}-param" type="range" min="${meta.parameterMin}" max="${meta.parameterMax}" value="${parameter}" oninput="lg.updateText('z${z}-l${l}-param-out', this.value + ' ${esc(meta.parameterUnit)}')"><output id="z${z}-l${l}-param-out">${parameter} ${esc(meta.parameterUnit)}</output></div>` : `<p class="help">${esc(meta.description)}</p>`}
                    <div><label class="switch-row"><span>Layer Enabled</span><input id="z${z}-l${l}-enabled" type="checkbox" ${layer.enabled ? 'checked' : ''}></label></div>
                </div>`;
            }).join('');
            return `<article class="product-card zone-card"><div class="zone-title"><h4>${esc(zName)}</h4></div>${layersHtml}</article>`;
        }).join('');
    }
    function changeZoneEffect(z, l, value) {
        readSceneEditor();
        state.editing.zones[z].layers[l].effect = value;
        state.editing.zones[z].layers[l].fxParam = effect(value).parameterDefault || 0;
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
        
        const roles = ['Unassigned', 'Sign', 'Sign Element', 'Ambiance Light', 'Main Light'];
        let zonesHtml = '';
        for (let i = 0; i < 8; ++i) {
            const z = state.config.zones?.[i] || {name: '', role: 'Unassigned'};
            zonesHtml += `<div class="field" style="margin-bottom:0.5rem;padding:0.5rem;background:rgba(0,0,0,0.2);border-radius:8px;">
                <div style="font-weight:bold;margin-bottom:0.5rem;">CH ${i}</div>
                <label>Name</label><input id="sys-zname-${i}" value="${esc(z.name)}" maxlength="23" placeholder="Zone ${i+1}">
                <label>Role</label><select id="sys-zrole-${i}">${roles.map(r => `<option value="${r}" ${z.role === r ? 'selected' : ''}>${r}</option>`).join('')}</select>
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
                zones: []
            };
            for (let i = 0; i < 8; ++i) {
                config.zones.push({
                    name: byId(`sys-zname-${i}`)?.value.trim() || '',
                    role: byId(`sys-zrole-${i}`)?.value || 'Unassigned'
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

    return {connect,disconnect,refresh,nav,playScene,blackout,editScene,backToScenes,renderSceneEditor,changeZoneEffect,changeOverlay,changeOverlayBeat,saveScene,updateText,speedText,selectPalette,dragScene,dropScene,selectStep,setStepScene,setStepLength,toggleTrack,setLoop,setBpm,setTimelineEnabled,saveTimeline,addCue,setCue,setCueTime,toggleDay,saveAutomation,saveSystem,refreshSync,uploadOta,downloadBackup,uploadRestore};
})();
</script>
</body>
</html>
)LGHTML";
