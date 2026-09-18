<script lang="ts">
  import { page } from '$app/stores';
  import { onMount } from 'svelte';
  import {
    AUTH_ROLE_KEY,
    AUTH_TIMESTAMP_KEY,
    NODE_IP_KEY,
    NODE_LIST_KEY,
    getSelectedNodeIp,
    setSelectedNodeIp,
    readString,
    readStringList,
    removeValue,
    writeString,
    writeStringList,
    unlockDashboardSession,
    lockDashboardSession,
  } from '$lib/auth/storage';
  import {
    connect,
    reconnect as reconnectNode,
    getNodeLiveState,
    getNodeLiveSnapshot,
  } from '$lib/state/nodeStore';

  // ── Feature 5: PIN Gate ──────────────────────────────────────────────────────
  const OPERATOR_HASH_KEY = 'lg_op_hash';
  const ADMIN_HASH_KEY    = 'lg_ad_hash';
  const DEFAULT_OP_PIN    = '1234';
  const DEFAULT_AD_PIN    = '9999';

  let authed      = $state(false);
  let isAdmin     = $state(false);
  let pinInput    = $state('');
  let pinError    = $state('');
  let showPinWall = $state(true);

  // Node management — browser persistence is isolated in auth/storage.
  let nodeIp   = $state('');
  let nodeList = $state<string[]>([]);
  let addIpVal = $state('');
  let liveState = $state(getNodeLiveSnapshot());
  let navListEl: HTMLUListElement | null = null;
  let mobileMoreOpen = $state(false);

  async function sha256(s: string): Promise<string> {
    const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s));
    return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
  }

  async function initHashes() {
    if (!readString(OPERATOR_HASH_KEY)) {
      writeString(OPERATOR_HASH_KEY, await sha256(DEFAULT_OP_PIN));
    }
    if (!readString(ADMIN_HASH_KEY)) {
      writeString(ADMIN_HASH_KEY, await sha256(DEFAULT_AD_PIN));
    }
  }

  async function submitPin() {
    pinError = '';
    const h = await sha256(pinInput);
    const opHash = readString(OPERATOR_HASH_KEY);
    const adHash = readString(ADMIN_HASH_KEY);
    if (h === adHash) {
      authed = true;
      isAdmin = true;
      showPinWall = false;
      unlockDashboardSession('admin');
    } else if (h === opHash) {
      authed = true;
      isAdmin = false;
      showPinWall = false;
      unlockDashboardSession('operator');
    } else {
      pinError = 'Incorrect PIN';
      pinInput = '';
    }
  }

  function lockDashboard() {
    authed = false;
    isAdmin = false;
    showPinWall = true;
    pinInput = '';
    pinError = '';
    lockDashboardSession();
  }

  function addNode() {
    const ip = addIpVal.trim();
    if (!ip || nodeList.includes(ip)) return;
    nodeList = [...nodeList, ip];
    writeStringList(NODE_LIST_KEY, nodeList);
    if (!nodeIp) selectNode(ip);
    addIpVal = '';
  }

  function removeNode(ip: string) {
    nodeList = nodeList.filter(n => n !== ip);
    writeStringList(NODE_LIST_KEY, nodeList);
    if (nodeIp === ip) selectNode(nodeList[0] ?? '');
  }

  function selectNode(ip: string) {
    nodeIp = ip;
    setSelectedNodeIp(ip);
    reconnectNode();
  }

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    nodeList = readStringList(NODE_LIST_KEY);
    initHashes();

    // Re-check auth on mount (session persists for 8 h via timestamp).
    const ts = Number(readString(AUTH_TIMESTAMP_KEY, '0'));
    if (Date.now() - ts < 8 * 3600 * 1000) {
      authed = true;
      isAdmin = readString(AUTH_ROLE_KEY) === 'admin';
      showPinWall = false;
      unlockDashboardSession(isAdmin ? 'admin' : 'operator');
    }

    // Keep the shared live connection warm for all dashboard routes.
    if (nodeIp) connect(nodeIp);

    const unsub = getNodeLiveState().subscribe(val => {
      liveState = val;
    });
    return unsub;
  });

  const navLinks = [
    { group: 'Overview', href: '/dashboard',              icon: '⬡',  label: 'Fleet' },
    { group: 'Create',   href: '/dashboard/scenes',       icon: '◆',  label: 'Scenes' },
    { group: 'Create',   href: '/dashboard/cues',         icon: '⏯',  label: 'Cues' },
    { group: 'Create',   href: '/dashboard/timeline',     icon: '▤',  label: 'Timeline' },
    { group: 'Create',   href: '/dashboard/schedule',     icon: '◷',  label: 'Schedule' },
    { group: 'Output',   href: '/dashboard/channels',     icon: '◈',  label: 'Channels' },
    { group: 'Output',   href: '/dashboard/visualizer',   icon: '▶',  label: 'Visualizer' },
    { group: 'Output',   href: '/dashboard/preview',      icon: '◉',  label: 'Preview' },
    { group: 'System',   href: '/dashboard/thermal',      icon: '⬥',  label: 'Thermal' },
    { group: 'System',   href: '/dashboard/uptime',       icon: '▣',  label: 'Uptime' },
    { group: 'System',   href: '/dashboard/diagnostics',  icon: '⚕',  label: 'Diagnostics' },
    { group: 'System',   href: '/dashboard/factory',      icon: '⚙',  label: 'Factory' },
    { group: 'System',   href: '/dashboard/firmware',     icon: '⟳',  label: 'Firmware' },
  ];

  const mobilePrimaryHrefs = new Set([
    '/dashboard',
    '/dashboard/scenes',
    '/dashboard/timeline',
    '/dashboard/channels',
  ]);
  const mobilePrimaryLinks = navLinks.filter(link => mobilePrimaryHrefs.has(link.href));
  const mobileMoreLinks = navLinks.filter(link => !mobilePrimaryHrefs.has(link.href));

  $effect(() => {
    // Route changes close the mobile utility sheet and keep the active desktop
    // sidebar destination in view when the list is taller than the viewport.
    $page.url.pathname;
    mobileMoreOpen = false;
    requestAnimationFrame(() => {
      navListEl?.querySelector<HTMLElement>('.nav-link.active')?.scrollIntoView({
        block: 'nearest',
        inline: 'nearest',
      });
    });
  });

  let { children } = $props();
</script>

{#if showPinWall}
<div class="pin-wall">
  <div class="pin-card">
    <div class="pin-logo">⬡</div>
    <h1>LumiGrid Operator</h1>
    <p class="pin-sub">Enter your access PIN to continue</p>
    <!-- svelte-ignore a11y_autofocus -->
    <input
      type="password"
      inputmode="numeric"
      maxlength="8"
      placeholder="• • • •"
      class="pin-input"
      bind:value={pinInput}
      onkeydown={(e) => e.key === 'Enter' && submitPin()}
      autofocus
    />
    {#if pinError}<p class="pin-error">{pinError}</p>{/if}
    <button class="pin-btn" onclick={submitPin}>Unlock</button>
    <p class="pin-hint">Default operator PIN: <code>1234</code> — Admin: <code>9999</code></p>
  </div>
</div>

{:else}
<div class="dash-root">
  <nav class="dash-nav" aria-label="Dashboard navigation">
    <a class="dash-brand" href="/dashboard">
      <span class="brand-hex">⬡</span>
      <span>LumiGrid</span>
    </a>

    <div class="node-section">
      <div class="nav-label-row">
        <p class="nav-label">ACTIVE NODE</p>
        {#if nodeIp}
          <span class="status-indicator {liveState.connection}" title={liveState.connection}>
            <span class="status-dot"></span>
            <span class="status-text">{liveState.connection === 'connected' ? 'Live' : liveState.connection === 'connecting' ? 'Connecting' : 'Offline'}</span>
          </span>
        {/if}
      </div>
      <select class="node-select" value={nodeIp} onchange={(e) => selectNode((e.target as HTMLSelectElement).value)}>
        {#if !nodeList.length}<option value="">— no nodes —</option>{/if}
        {#each nodeList as ip}
          <option value={ip}>{ip}</option>
        {/each}
      </select>
      {#if nodeIp && liveState.connection === 'connected'}
        <div class="node-live-telemetry">
          {#if liveState.health}
            <span class="telemetry-pill" class:fault={liveState.health.output.fault} title="Output state">
              {liveState.health.output.state}
            </span>
          {/if}
          {#if liveState.status?.effect}
            <span class="telemetry-pill effect" title="Current effect">
              {liveState.status.effect}
            </span>
          {/if}
        </div>
      {/if}
      <div class="node-add-row">
        <input
          class="node-add-input"
          placeholder="192.168.x.x"
          bind:value={addIpVal}
          onkeydown={(e) => e.key === 'Enter' && addNode()}
        />
        <button class="icon-btn" onclick={addNode} title="Add node" aria-label="Add node">+</button>
      </div>
      {#each nodeList as ip}
        <div class="node-chip">
          <button class="node-chip-select" class:active={nodeIp === ip} onclick={() => selectNode(ip)}>{ip}</button>
          <button class="node-chip-del" onclick={() => removeNode(ip)} title="Remove node" aria-label={`Remove node ${ip}`}>×</button>
        </div>
      {/each}
    </div>

    <ul class="nav-links" role="list" bind:this={navListEl}>
      {#each navLinks as { group, href, icon, label }, i}
        {#if i === 0 || navLinks[i - 1].group !== group}
          <li class="nav-group-label" aria-hidden="true">{group}</li>
        {/if}
        <li>
          <a
            {href}
            class="nav-link"
            class:active={$page.url.pathname === href}
            aria-current={$page.url.pathname === href ? 'page' : undefined}
          >
            <span class="nav-icon">{icon}</span>
            <span>{label}</span>
          </a>
        </li>
      {/each}
    </ul>

    <div class="role-badge" class:admin={isAdmin}>
      {isAdmin ? '◈ Admin' : '◦ Operator'}
    </div>
    <button class="logout-btn" onclick={lockDashboard} title="Lock dashboard">
      Lock
    </button>
  </nav>

  <div class="mobile-topbar">
    <a class="mobile-brand" href="/dashboard" aria-label="LumiGrid fleet">⬡</a>
    <select
      class="mobile-node-select"
      aria-label="Active node"
      value={nodeIp}
      onchange={(e) => selectNode((e.target as HTMLSelectElement).value)}
    >
      {#if !nodeList.length}<option value="">No nodes</option>{/if}
      {#each nodeList as ip}
        <option value={ip}>{ip}</option>
      {/each}
    </select>
    <span class="mobile-status {liveState.connection}" title={liveState.connection}>
      <span class="status-dot"></span>
      <span>{liveState.connection === 'connected' ? 'Live' : liveState.connection === 'connecting' ? '…' : 'Off'}</span>
    </span>
    <button class="mobile-lock" onclick={lockDashboard} title="Lock dashboard" aria-label="Lock dashboard">⌁</button>
  </div>

  <nav class="mobile-bottom-nav" aria-label="Primary dashboard navigation">
    {#each mobilePrimaryLinks as { href, icon, label }}
      <a
        {href}
        class="mobile-nav-link"
        class:active={$page.url.pathname === href}
        aria-current={$page.url.pathname === href ? 'page' : undefined}
      >
        <span class="mobile-nav-icon">{icon}</span>
        <span>{label}</span>
      </a>
    {/each}
    <button
      class="mobile-nav-link mobile-more-button"
      class:active={mobileMoreLinks.some(link => $page.url.pathname === link.href) || mobileMoreOpen}
      aria-expanded={mobileMoreOpen}
      aria-controls="mobile-more-sheet"
      onclick={() => (mobileMoreOpen = !mobileMoreOpen)}
      type="button"
    >
      <span class="mobile-nav-icon">•••</span>
      <span>More</span>
    </button>
  </nav>

  {#if mobileMoreOpen}
    <div
      class="mobile-more-backdrop"
      role="presentation"
      onclick={(e) => {
        if (e.target === e.currentTarget) mobileMoreOpen = false;
      }}
    >
      <section
        id="mobile-more-sheet"
        class="mobile-more-sheet"
        role="dialog"
        aria-modal="true"
        aria-label="More dashboard tools"
        tabindex="-1"
        onkeydown={(e) => { if (e.key === 'Escape') mobileMoreOpen = false; }}
      >
        <div class="mobile-more-header">
          <div>
            <strong>More tools</strong>
            <span>Creation, monitoring and system utilities</span>
          </div>
          <button
            class="mobile-more-close"
            onclick={() => (mobileMoreOpen = false)}
            type="button"
            aria-label="Close more tools"
          >✕</button>
        </div>
        <div class="mobile-more-grid">
          {#each mobileMoreLinks as { group, href, icon, label }}
            <a
              {href}
              class="mobile-more-link"
              class:active={$page.url.pathname === href}
              aria-current={$page.url.pathname === href ? 'page' : undefined}
            >
              <span class="mobile-more-icon">{icon}</span>
              <span class="mobile-more-copy">
                <strong>{label}</strong>
                <small>{group}</small>
              </span>
            </a>
          {/each}
        </div>
      </section>
    </div>
  {/if}

  <main class="dash-main">
    {@render children()}
  </main>
</div>
{/if}

<style>
  :global(body) { margin: 0; }

  :global(.dash-root),
  :global(.pin-wall) {
    /* Operator Console is deliberately dark. Do not inherit the public
       Lander's prefers-color-scheme: light token overrides. */
    --dash-text: #f5f5f7;
    --dash-muted: #a1a1a6;
    --dash-surface: rgba(29,29,31,.72);
    --dash-surface-strong: rgba(29,29,31,.92);
    --dash-border: rgba(255,255,255,.10);
    color-scheme: dark;
    text-rendering: optimizeLegibility;
    -webkit-font-smoothing: antialiased;
  }

  :global(.dash-root ::selection),
  :global(.pin-wall ::selection) {
    background: rgba(var(--dash-accent-rgb), .32);
    color: #fff;
  }

  :global(.dash-root button),
  :global(.dash-root a),
  :global(.dash-root input),
  :global(.dash-root select),
  :global(.dash-root textarea),
  :global(.dash-root [role="button"]),
  :global(.pin-wall button),
  :global(.pin-wall input) {
    -webkit-tap-highlight-color: transparent;
  }

  :global(.dash-root button:focus-visible),
  :global(.dash-root a:focus-visible),
  :global(.dash-root input:focus-visible),
  :global(.dash-root select:focus-visible),
  :global(.dash-root textarea:focus-visible),
  :global(.dash-root [role="button"]:focus-visible),
  :global(.pin-wall button:focus-visible),
  :global(.pin-wall input:focus-visible) {
    outline: 2px solid var(--dash-accent);
    outline-offset: 2px;
    box-shadow: 0 0 0 4px rgba(var(--dash-accent-rgb), .12);
  }

  :global(.dash-root input),
  :global(.dash-root select),
  :global(.dash-root textarea) {
    caret-color: var(--dash-accent);
  }

  :global(.dash-root select option) {
    background: #171719;
    color: #f5f5f7;
  }

  :global(.dash-root button:disabled),
  :global(.dash-root input:disabled),
  :global(.dash-root select:disabled) {
    cursor: not-allowed;
  }

  :global(.dash-root .loading),
  :global(.dash-root .diag-loading),
  :global(.dash-root .diag-empty),
  :global(.dash-root .loading-state),
  :global(.dash-root .empty-state),
  :global(.dash-root .loading-box),
  :global(.dash-root .empty-box),
  :global(.dash-root .loading-panel),
  :global(.dash-root .empty-panel),
  :global(.dash-root .tl-loading),
  :global(.dash-root .tl-empty) {
    min-height: 220px;
    display: grid;
    place-items: center;
    align-content: center;
    gap: .75rem;
    padding: 2rem;
    color: var(--dash-muted);
    text-align: center;
    background: linear-gradient(180deg, rgba(29,29,31,.56), rgba(18,18,20,.48));
    border: 1px dashed rgba(255,255,255,.10);
    border-radius: var(--dash-radius-lg);
  }

  :global(.dash-root .toast),
  :global(.dash-root .toast-bar),
  :global(.dash-root .toast-pill) {
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid rgba(255,255,255,.12);
    box-shadow: 0 14px 36px rgba(0,0,0,.36);
  }

  :global(.dash-root .error-bar),
  :global(.dash-root .error-banner),
  :global(.dash-root .err-box) {
    backdrop-filter: blur(16px);
    -webkit-backdrop-filter: blur(16px);
    box-shadow: 0 10px 30px rgba(0,0,0,.24);
  }

  /* The public Lander intentionally gives headings animated gradients and all
     buttons a 44px minimum. The operator app uses the same design tokens, but
     needs denser controls and calmer typography. Keep those concerns isolated. */
  :global(.dash-root h1),
  :global(.dash-root h2),
  :global(.dash-root h3),
  :global(.pin-wall h1) {
    background: none;
    background-image: none;
    -webkit-background-clip: initial;
    background-clip: initial;
    -webkit-text-fill-color: currentColor;
    color: inherit;
    animation: none;
  }
  :global(.dash-root button),
  :global(.pin-wall button) {
    min-height: 0;
  }
  :global(.dash-root input),
  :global(.dash-root select),
  :global(.dash-root textarea) {
    font-family: inherit;
  }


  .pin-wall {
    min-height: 100dvh;
    display: flex;
    align-items: center;
    justify-content: center;
    background: radial-gradient(ellipse at 20% 0%, rgba(255,45,85,.12) 0%, transparent 55%),
                radial-gradient(ellipse at 80% 100%, rgba(255,149,0,.06) 0%, transparent 55%),
                #050505;
  }
  .pin-card {
    background: rgba(29,29,31,.88);
    backdrop-filter: blur(24px);
    border: 1px solid rgba(255,255,255,.10);
    border-radius: 24px;
    padding: 2.5rem 2rem;
    width: min(360px, 92vw);
    text-align: center;
    box-shadow: 0 24px 64px rgba(0,0,0,.6);
  }
  .pin-logo {
    font-size: 3rem;
    background: linear-gradient(135deg, var(--dash-accent), var(--dash-warm));
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
    margin-bottom: .5rem;
  }
  .pin-card h1 {
    margin: 0 0 .4rem;
    font-size: 1.5rem;
    color: #f5f5f7;
    font-family: -apple-system, BlinkMacSystemFont, Inter, sans-serif;
    font-weight: 700;
  }
  .pin-sub { color: #a1a1a6; font-size: .9rem; margin: 0 0 1.5rem; }
  .pin-input {
    width: 100%;
    padding: .9rem 1rem;
    font-size: 1.5rem;
    letter-spacing: .4em;
    text-align: center;
    background: rgba(255,255,255,.06);
    border: 1px solid rgba(255,255,255,.14);
    border-radius: 12px;
    color: #f5f5f7;
    font-family: monospace;
    margin-bottom: .75rem;
    box-sizing: border-box;
  }
  .pin-input:focus-visible { border-color: var(--dash-accent); }
  .pin-error { color: #ff453a; font-size: .85rem; margin: -.4rem 0 .6rem; }
  .pin-btn {
    width: 100%;
    padding: .85rem;
    background: rgba(255,45,85,.88);
    color: #fff;
    border: none;
    border-radius: 999px;
    font-size: 1rem;
    font-weight: 600;
    cursor: pointer;
    transition: background .2s, box-shadow .2s, transform .15s;
    box-shadow: 0 4px 16px rgba(255,45,85,.40);
    margin-bottom: 1rem;
  }
  .pin-btn:hover { background: #ff3b30; transform: translateY(-1px); box-shadow: 0 8px 24px rgba(255,45,85,.55); }
  .pin-hint { color: #a1a1a6; font-size: .78rem; margin: 0; }
  .pin-hint code { color: #f5f5f7; background: rgba(255,255,255,.08); padding: .1em .35em; border-radius: 4px; }

  .dash-root {
    display: flex;
    min-height: 100dvh;
    background: radial-gradient(ellipse at 15% 0%, rgba(255,45,85,.09) 0%, transparent 50%),
                radial-gradient(ellipse at 85% 100%, rgba(255,149,0,.045) 0%, transparent 50%),
                #050505;
    color: #f5f5f7;
    font-family: -apple-system, BlinkMacSystemFont, Inter, system-ui, sans-serif;
  }
  .dash-nav {
    width: 220px;
    min-height: 100dvh;
    background: rgba(15,15,17,.92);
    backdrop-filter: blur(20px);
    border-right: 1px solid rgba(255,255,255,.08);
    display: flex;
    flex-direction: column;
    padding: 1.25rem 1rem;
    gap: 1rem;
    flex-shrink: 0;
    position: sticky;
    top: 0;
    height: 100dvh;
    overflow-y: auto;
  }
  .dash-brand {
    display: flex;
    align-items: center;
    gap: .5rem;
    font-size: 1.05rem;
    font-weight: 700;
    color: #f5f5f7;
    text-decoration: none;
    padding: .25rem .25rem .75rem;
    border-bottom: 1px solid rgba(255,255,255,.08);
  }
  .brand-hex {
    font-size: 1.4rem;
    background: linear-gradient(135deg, var(--dash-accent), var(--dash-warm));
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
  }
  .node-section { display: flex; flex-direction: column; gap: .4rem; }
  .nav-label { font-size: .65rem; font-weight: 700; letter-spacing: .08em; color: #a1a1a6; margin: 0 0 .2rem; text-transform: uppercase; }
  .node-select {
    width: 100%;
    background: rgba(255,255,255,.06);
    border: 1px solid rgba(255,255,255,.12);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: .82rem;
    padding: .35rem .5rem;
    font-family: monospace;
  }
  .node-add-row { display: flex; gap: .4rem; }
  .node-add-input {
    flex: 1;
    background: rgba(255,255,255,.05);
    border: 1px solid rgba(255,255,255,.10);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: .78rem;
    padding: .3rem .45rem;
    font-family: monospace;
  }
  .node-add-input:focus-visible { border-color: var(--dash-accent); }
  .icon-btn {
    background: rgba(255,45,85,.75);
    color: #fff;
    border: none;
    border-radius: 8px;
    width: 28px;
    height: 28px;
    cursor: pointer;
    font-size: 1rem;
    line-height: 1;
    transition: background .2s;
  }
  .icon-btn:hover { background: #ff3b30; }
  .node-chip { display: flex; align-items: center; gap: .25rem; }
  .node-chip-select {
    flex: 1;
    background: rgba(255,255,255,.04);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 6px;
    color: #a1a1a6;
    font-size: .72rem;
    padding: .2rem .4rem;
    text-align: left;
    cursor: pointer;
    font-family: monospace;
    transition: background .2s, color .2s;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .node-chip-select.active { background: rgba(255,45,85,.18); border-color: rgba(255,45,85,.4); color: #f5f5f7; }
  .node-chip-del {
    background: transparent;
    border: none;
    color: #a1a1a6;
    cursor: pointer;
    font-size: .9rem;
    padding: .1rem .3rem;
    border-radius: 4px;
    transition: color .2s;
  }
  .node-chip-del:hover { color: #ff453a; }
  .nav-links {
    list-style: none;
    margin: .5rem 0 0;
    padding: 0 .15rem .5rem 0;
    display: flex;
    flex-direction: column;
    gap: .15rem;
    overflow-y: auto;
    min-height: 0;
    scrollbar-width: none;
  }
  .nav-links::-webkit-scrollbar { display: none; }
  .nav-group-label {
    list-style: none;
    margin: .65rem .55rem .15rem;
    color: #636366;
    font-size: .58rem;
    font-weight: 800;
    letter-spacing: .1em;
    text-transform: uppercase;
  }
  .mobile-topbar,
  .mobile-bottom-nav,
  .mobile-more-backdrop { display: none; }
  .nav-link {
    display: flex;
    align-items: center;
    gap: .55rem;
    padding: .55rem .7rem;
    border-radius: 10px;
    color: #a1a1a6;
    font-size: .88rem;
    font-weight: 500;
    text-decoration: none;
    transition: background .18s, color .18s;
  }
  .nav-link:hover { background: rgba(255,255,255,.06); color: #f5f5f7; }
  .nav-link.active {
    background: linear-gradient(90deg, rgba(var(--dash-accent-rgb),.19), rgba(var(--dash-accent-rgb),.08));
    color: #fff;
    box-shadow: inset 0 0 0 1px rgba(var(--dash-accent-rgb),.16), 0 8px 22px rgba(0,0,0,.16);
  }
  .nav-link.active .nav-icon { color: var(--dash-accent); filter: drop-shadow(0 0 7px rgba(var(--dash-accent-rgb),.35)); }
  .nav-icon { font-size: 1rem; width: 1.2rem; text-align: center; }
  .role-badge {
    margin-top: auto;
    padding: .3rem .6rem;
    background: rgba(255,255,255,.05);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 999px;
    font-size: .72rem;
    color: #a1a1a6;
    text-align: center;
  }
  .role-badge.admin { border-color: rgba(255,149,0,.3); color: #ff9500; background: rgba(255,149,0,.08); }
  .logout-btn {
    background: transparent;
    border: 1px solid rgba(255,255,255,.10);
    border-radius: 8px;
    color: #a1a1a6;
    font-size: .78rem;
    padding: .35rem;
    cursor: pointer;
    transition: border-color .2s, color .2s;
  }
  .logout-btn:hover { border-color: #ff453a; color: #ff453a; }
  .dash-main {
    flex: 1;
    min-width: 0;
    padding: clamp(1.25rem, 2.4vw, 2.25rem);
    overflow-y: auto;
    max-height: 100dvh;
    overscroll-behavior-y: contain;
    scrollbar-gutter: stable;
    scrollbar-color: rgba(255,255,255,.14) transparent;
    scrollbar-width: thin;
  }
  .nav-label-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: .4rem;
  }
  .nav-label-row .nav-label { margin: 0; }
  .status-indicator {
    display: inline-flex;
    align-items: center;
    gap: .3rem;
    font-size: .68rem;
    font-weight: 600;
  }
  .status-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #8e8e93;
  }
  .status-indicator.connected .status-dot { background: #30d158; box-shadow: 0 0 6px rgba(48,209,88,.6); }
  .status-indicator.connected .status-text { color: #30d158; }
  .status-indicator.connecting .status-dot { background: var(--dash-warm); }
  .status-indicator.connecting .status-text { color: var(--dash-warm); }
  .status-indicator.disconnected .status-dot { background: #8e8e93; }
  .status-indicator.disconnected .status-text { color: #8e8e93; }
  .status-indicator.error .status-dot { background: #ff453a; }
  .status-indicator.error .status-text { color: #ff453a; }
  .node-live-telemetry {
    display: flex;
    gap: .35rem;
    margin-top: .35rem;
    flex-wrap: wrap;
  }
  .telemetry-pill {
    font-size: .68rem;
    padding: .15rem .45rem;
    border-radius: 6px;
    background: rgba(48,209,88,.12);
    color: #30d158;
    border: 1px solid rgba(48,209,88,.25);
    font-weight: 600;
  }
  .telemetry-pill.fault {
    background: rgba(255,69,58,.15);
    color: #ff453a;
    border-color: rgba(255,69,58,.3);
  }
  .telemetry-pill.effect {
    background: rgba(0,212,255,.10);
    color: #00d4ff;
    border-color: rgba(0,212,255,.2);
  }
  @media (prefers-reduced-motion: reduce) {
    :global(.dash-root *),
    :global(.dash-root *::before),
    :global(.dash-root *::after),
    :global(.pin-wall *),
    :global(.pin-wall *::before),
    :global(.pin-wall *::after) {
      scroll-behavior: auto !important;
      animation-duration: .01ms !important;
      animation-iteration-count: 1 !important;
      transition-duration: .01ms !important;
    }
  }

  @media (max-width: 980px) {
    .dash-nav { width: 196px; padding-inline: .75rem; }
    .nav-link { padding-inline: .55rem; font-size: .82rem; }
  }

  @media (max-width: 768px) {
    .dash-root {
      display: block;
      min-height: 100dvh;
      padding-bottom: calc(78px + env(safe-area-inset-bottom));
    }
    .dash-nav { display: none; }
    .mobile-topbar {
      position: fixed;
      inset: env(safe-area-inset-top) 0 auto 0;
      height: 54px;
      z-index: 1190;
      display: grid;
      grid-template-columns: 34px minmax(0, 1fr) auto 34px;
      align-items: center;
      gap: 8px;
      padding: 7px 10px;
      background: rgba(15,15,17,.94);
      backdrop-filter: blur(20px);
      -webkit-backdrop-filter: blur(20px);
      border-bottom: 1px solid rgba(255,255,255,.08);
      box-shadow: 0 10px 30px rgba(0,0,0,.22);
    }
    .mobile-brand {
      display: grid;
      place-items: center;
      color: var(--dash-accent);
      text-decoration: none;
      font-size: 1.35rem;
      font-weight: 700;
    }
    .mobile-node-select {
      width: 100%;
      min-width: 0;
      height: 38px;
      background: rgba(255,255,255,.06);
      border: 1px solid rgba(255,255,255,.11);
      border-radius: 10px;
      color: #f5f5f7;
      padding: 0 .65rem;
      font: .78rem ui-monospace, SFMono-Regular, Menlo, monospace;
    }
    .mobile-status {
      display: inline-flex;
      align-items: center;
      gap: 4px;
      color: #8e8e93;
      font-size: .65rem;
      font-weight: 700;
    }
    .mobile-status.connected { color: var(--dash-ok); }
    .mobile-status.connecting { color: var(--dash-warm); }
    .mobile-lock {
      width: 34px;
      height: 34px;
      display: grid;
      place-items: center;
      border: 1px solid rgba(255,255,255,.1);
      border-radius: 10px;
      background: rgba(255,255,255,.04);
      color: #a1a1a6;
      cursor: pointer;
    }

    .mobile-bottom-nav {
      position: fixed;
      inset: auto 0 0 0;
      z-index: 1200;
      display: grid;
      grid-template-columns: repeat(5, minmax(0, 1fr));
      gap: 4px;
      padding: 7px 8px calc(7px + env(safe-area-inset-bottom));
      border-top: 1px solid rgba(255,255,255,.1);
      background: rgba(15,15,17,.95);
      backdrop-filter: blur(22px);
      -webkit-backdrop-filter: blur(22px);
      box-shadow: 0 -14px 40px rgba(0,0,0,.38);
    }
    .mobile-nav-link {
      min-width: 0;
      min-height: 56px !important;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 3px;
      padding: 5px 3px;
      border: 1px solid transparent;
      border-radius: 12px;
      background: transparent;
      color: #8e8e93;
      text-decoration: none;
      font: 700 .62rem/1.05 -apple-system, BlinkMacSystemFont, Inter, system-ui, sans-serif;
      cursor: pointer;
    }
    .mobile-nav-link.active {
      color: #fff;
      background: linear-gradient(180deg, rgba(var(--dash-accent-rgb),.21), rgba(var(--dash-accent-rgb),.09));
      border-color: rgba(var(--dash-accent-rgb),.22);
      box-shadow: inset 0 1px 0 rgba(255,255,255,.035);
    }
    .mobile-nav-icon {
      min-height: 18px;
      display: grid;
      place-items: center;
      color: currentColor;
      font-size: 1.08rem;
      line-height: 1;
    }
    .mobile-more-button { width: 100%; }
    .mobile-more-button .mobile-nav-icon {
      letter-spacing: .08em;
      font-size: .85rem;
    }

    .mobile-more-backdrop {
      position: fixed;
      inset: 0;
      z-index: 1195;
      display: flex;
      align-items: flex-end;
      padding: 70px .7rem calc(78px + env(safe-area-inset-bottom));
      background: rgba(0,0,0,.52);
      backdrop-filter: blur(8px);
      -webkit-backdrop-filter: blur(8px);
    }
    .mobile-more-sheet {
      width: min(100%, 560px);
      max-height: min(68dvh, 560px);
      margin: 0 auto;
      overflow: auto;
      background: rgba(22,22,24,.97);
      border: 1px solid rgba(255,255,255,.12);
      border-radius: 22px;
      padding: .85rem;
      box-shadow: 0 24px 70px rgba(0,0,0,.58);
      scrollbar-width: thin;
      scrollbar-color: rgba(255,255,255,.16) transparent;
    }
    .mobile-more-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 1rem;
      padding: .25rem .25rem .8rem;
    }
    .mobile-more-header > div {
      display: flex;
      flex-direction: column;
      gap: .1rem;
    }
    .mobile-more-header strong {
      font-size: .98rem;
      color: #f5f5f7;
    }
    .mobile-more-header span {
      color: #8e8e93;
      font-size: .72rem;
    }
    .mobile-more-close {
      width: 34px;
      height: 34px;
      display: grid;
      place-items: center;
      border: 1px solid rgba(255,255,255,.10);
      border-radius: 10px;
      background: rgba(255,255,255,.045);
      color: #a1a1a6;
      cursor: pointer;
    }
    .mobile-more-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: .55rem;
    }
    .mobile-more-link {
      min-width: 0;
      min-height: 76px;
      display: flex;
      align-items: center;
      gap: .65rem;
      padding: .7rem;
      border: 1px solid rgba(255,255,255,.075);
      border-radius: 14px;
      background: rgba(255,255,255,.035);
      color: #d1d1d6;
      text-decoration: none;
    }
    .mobile-more-link.active {
      border-color: rgba(var(--dash-accent-rgb),.28);
      background: linear-gradient(135deg, rgba(var(--dash-accent-rgb),.17), rgba(var(--dash-accent-rgb),.065));
      color: #fff;
    }
    .mobile-more-icon {
      flex: 0 0 28px;
      display: grid;
      place-items: center;
      color: var(--dash-accent);
      font-size: 1.05rem;
    }
    .mobile-more-copy {
      min-width: 0;
      display: flex;
      flex-direction: column;
      gap: .12rem;
    }
    .mobile-more-copy strong {
      overflow: hidden;
      text-overflow: ellipsis;
      font-size: .78rem;
      white-space: nowrap;
    }
    .mobile-more-copy small {
      color: #737378;
      font-size: .62rem;
      text-transform: uppercase;
      letter-spacing: .05em;
    }

    .dash-main {
      padding: calc(1rem + 54px + env(safe-area-inset-top)) 1rem 1rem;
      max-height: none;
      overflow: visible;
    }
  }

  @media (max-width: 430px) {
    .mobile-more-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  }
</style>
