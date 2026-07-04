<script lang="ts">
  import { page } from '$app/stores';
  import { onMount } from 'svelte';

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

  // Node management — shared context for all child pages
  let nodeIp   = $state(localStorage?.getItem('lg_node_ip') ?? '');
  let nodeList = $state<string[]>(JSON.parse(localStorage?.getItem('lg_nodes') ?? '[]'));
  let addIpVal = $state('');

  async function sha256(s: string): Promise<string> {
    const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s));
    return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
  }

  async function initHashes() {
    if (!localStorage.getItem(OPERATOR_HASH_KEY)) {
      localStorage.setItem(OPERATOR_HASH_KEY, await sha256(DEFAULT_OP_PIN));
    }
    if (!localStorage.getItem(ADMIN_HASH_KEY)) {
      localStorage.setItem(ADMIN_HASH_KEY, await sha256(DEFAULT_AD_PIN));
    }
  }

  async function submitPin() {
    pinError = '';
    const h = await sha256(pinInput);
    const opHash = localStorage.getItem(OPERATOR_HASH_KEY);
    const adHash = localStorage.getItem(ADMIN_HASH_KEY);
    if (h === adHash) { authed = true; isAdmin = true; showPinWall = false; }
    else if (h === opHash) { authed = true; isAdmin = false; showPinWall = false; }
    else { pinError = 'Incorrect PIN'; pinInput = ''; }
  }

  function addNode() {
    const ip = addIpVal.trim();
    if (!ip || nodeList.includes(ip)) return;
    nodeList = [...nodeList, ip];
    localStorage.setItem('lg_nodes', JSON.stringify(nodeList));
    if (!nodeIp) selectNode(ip);
    addIpVal = '';
  }

  function removeNode(ip: string) {
    nodeList = nodeList.filter(n => n !== ip);
    localStorage.setItem('lg_nodes', JSON.stringify(nodeList));
    if (nodeIp === ip) selectNode(nodeList[0] ?? '');
  }

  function selectNode(ip: string) {
    nodeIp = ip;
    localStorage.setItem('lg_node_ip', ip);
  }

  onMount(() => {
    initHashes();
    // Re-check auth on mount (session persists for 8 h via timestamp)
    const ts = Number(localStorage.getItem('lg_auth_ts') ?? 0);
    if (Date.now() - ts < 8 * 3600 * 1000) {
      authed = true;
      isAdmin = localStorage.getItem('lg_auth_role') === 'admin';
      showPinWall = false;
    }
  });

  $effect(() => {
    if (authed) {
      localStorage.setItem('lg_auth_ts', String(Date.now()));
      localStorage.setItem('lg_auth_role', isAdmin ? 'admin' : 'operator');
    }
  });

  const navLinks = [
    { href: '/dashboard',          icon: '⬡',  label: 'Fleet' },
    { href: '/dashboard/channels', icon: '◈',  label: 'Channels' },
    { href: '/dashboard/thermal',  icon: '⬥',  label: 'Thermal' },
    { href: '/dashboard/schedule', icon: '◷',  label: 'Schedule' },
    { href: '/dashboard/uptime',   icon: '▣',  label: 'Uptime' },
    { href: '/dashboard/preview',  icon: '◉',  label: 'Preview' },
    { href: '/dashboard/firmware', icon: '⟳',  label: 'Firmware' },
  ];

  let { children } = $props();
</script>

{#if showPinWall}
<!-- ── PIN wall ─────────────────────────────────────────────────────────────── -->
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
<!-- ── Dashboard shell ─────────────────────────────────────────────────────── -->
<div class="dash-root">
  <!-- Sidebar -->
  <nav class="dash-nav" aria-label="Dashboard navigation">
    <a class="dash-brand" href="/dashboard">
      <span class="brand-hex">⬡</span>
      <span>LumiGrid</span>
    </a>

    <!-- Node selector -->
    <div class="node-section">
      <p class="nav-label">ACTIVE NODE</p>
      <select class="node-select" value={nodeIp} onchange={(e) => selectNode((e.target as HTMLSelectElement).value)}>
        {#if !nodeList.length}<option value="">— no nodes —</option>{/if}
        {#each nodeList as ip}
          <option value={ip}>{ip}</option>
        {/each}
      </select>
      <div class="node-add-row">
        <input
          class="node-add-input"
          placeholder="192.168.x.x"
          bind:value={addIpVal}
          onkeydown={(e) => e.key === 'Enter' && addNode()}
        />
        <button class="icon-btn" onclick={addNode} title="Add node">+</button>
      </div>
      {#each nodeList as ip}
        <div class="node-chip">
          <button class="node-chip-select" class:active={nodeIp === ip} onclick={() => selectNode(ip)}>{ip}</button>
          <button class="node-chip-del" onclick={() => removeNode(ip)} title="Remove">×</button>
        </div>
      {/each}
    </div>

    <!-- Navigation links -->
    <ul class="nav-links" role="list">
      {#each navLinks as { href, icon, label }}
        <li>
          <a {href} class="nav-link" class:active={$page.url.pathname === href}>
            <span class="nav-icon">{icon}</span>
            <span>{label}</span>
          </a>
        </li>
      {/each}
    </ul>

    <!-- Role badge -->
    <div class="role-badge" class:admin={isAdmin}>
      {isAdmin ? '◈ Admin' : '◦ Operator'}
    </div>
    <button class="logout-btn" onclick={() => { authed = false; showPinWall = true; localStorage.removeItem('lg_auth_ts'); }}>
      Lock
    </button>
  </nav>

  <!-- Main content -->
  <main class="dash-main">
    <!-- Pass nodeIp + isAdmin via context trick — children read from localStorage directly -->
    {@render children()}
  </main>
</div>
{/if}

<style>
  :global(body) { margin: 0; }

  /* PIN wall */
  .pin-wall {
    min-height: 100dvh;
    display: flex;
    align-items: center;
    justify-content: center;
    background: radial-gradient(ellipse at 20% 0%, rgba(255,45,85,.12) 0%, transparent 55%),
                radial-gradient(ellipse at 80% 100%, rgba(0,212,255,.07) 0%, transparent 55%),
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
    background: linear-gradient(135deg, #ff2d55, #00d4ff);
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
  .pin-input:focus { outline: none; border-color: #ff2d55; box-shadow: 0 0 0 2px rgba(255,45,85,.25); }
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

  /* Dashboard shell */
  .dash-root {
    display: flex;
    min-height: 100dvh;
    background: radial-gradient(ellipse at 15% 0%, rgba(255,45,85,.09) 0%, transparent 50%),
                radial-gradient(ellipse at 85% 100%, rgba(0,212,255,.06) 0%, transparent 50%),
                #050505;
    color: #f5f5f7;
    font-family: -apple-system, BlinkMacSystemFont, Inter, system-ui, sans-serif;
  }

  /* Sidebar */
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
    background: linear-gradient(135deg, #ff2d55, #00d4ff);
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
  }

  /* Node section */
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
  .node-add-input:focus { outline: none; border-color: #ff2d55; }
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

  /* Nav links */
  .nav-links { list-style: none; margin: .5rem 0 0; padding: 0; display: flex; flex-direction: column; gap: .15rem; }
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
  .nav-link.active { background: rgba(255,45,85,.18); color: #f5f5f7; border-left: 2px solid #ff2d55; }
  .nav-icon { font-size: 1rem; width: 1.2rem; text-align: center; }

  /* Role badge */
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

  /* Main content area */
  .dash-main {
    flex: 1;
    padding: 2rem;
    overflow-y: auto;
    max-height: 100dvh;
  }

  @media (max-width: 768px) {
    .dash-nav { width: 56px; padding: 1rem .5rem; }
    .dash-brand span:last-child,
    .nav-link span:last-child,
    .node-section,
    .role-badge,
    .logout-btn { display: none; }
    .dash-main { padding: 1rem; }
  }
</style>
