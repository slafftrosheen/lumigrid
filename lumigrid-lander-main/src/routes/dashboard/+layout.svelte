<script lang="ts">
  import { page } from '$app/stores';
  import { onMount } from 'svelte';
  import {
    getSelectedNodeIp,
    setSelectedNodeIp,
    getSavedNodes,
    NODE_REGISTRY_CHANGED_EVENT,
    type SavedNodeInfo,
  } from '$lib/auth/storage';
  import { nodeAuthStore, type NodeAuthSession } from '$lib/auth/nodeAuth';
  import {
    getAuthStatus,
    bootstrapAuth,
    createSession,
    deleteSession,
    getCredentialsStatus,
    rotateCredential,
    disableCredential,
    type AuthStatusResponse,
    type CredentialsStatusResponse,
  } from '$lib/api/auth';
  import {
    connect,
    selectNode as selectLiveNode,
    getNodeLiveState,
    getNodeLiveSnapshot,
  } from '$lib/state/nodeStore';

  // Active node & fleet
  let nodeIp   = $state('');
  let nodeList = $state<SavedNodeInfo[]>([]);
  let liveState = $state(getNodeLiveSnapshot());
  let navListEl = $state<HTMLUListElement | null>(null);
  let mobileMoreOpen = $state(false);

  // Per-node authoritative session state
  let currentSession = $state<NodeAuthSession | null>(null);
  let authed         = $derived(currentSession !== null);
  let userRole       = $derived(currentSession?.role ?? 'unknown');
  let isAdmin        = $derived(userRole === 'admin');

  // Node auth status & login/bootstrap form state
  let authStatus       = $state<AuthStatusResponse | null>(null);
  let authStatusError  = $state('');
  let authLoading      = $state(false);
  let authSubmitting   = $state(false);
  let authError        = $state('');
  let loginKeyInput    = $state('');
  let bootstrapKeyInput = $state('');

  // Access Control Modal (Admin-only)
  let showAccessControl = $state(false);
  let credsStatus       = $state<CredentialsStatusResponse | null>(null);
  let credsLoading      = $state(false);
  let credsError        = $state('');
  let credsSuccess      = $state('');
  let rotateRole        = $state<'admin' | 'installer' | 'operator'>('admin');
  let newKeyInput       = $state('');
  let keyCopied         = $state(false);

  function generateSecureKey(): string {
    const charset = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_';
    const bytes = new Uint8Array(24);
    crypto.getRandomValues(bytes);
    return Array.from(bytes, b => charset[b % charset.length]).join('');
  }

  async function checkNodeAuth(ip: string) {
    if (!ip) {
      authStatus = null;
      authStatusError = '';
      return;
    }
    const existing = nodeAuthStore.getSession(ip);
    if (existing) {
      currentSession = existing;
      return;
    }
    currentSession = null;
    authLoading = true;
    authStatusError = '';
    authError = '';
    try {
      authStatus = await getAuthStatus(ip);
    } catch (err) {
      authStatusError = err instanceof Error ? err.message : String(err);
      authStatus = null;
    } finally {
      authLoading = false;
    }
  }

  async function submitBootstrap() {
    if (!nodeIp) return;
    const key = bootstrapKeyInput.trim();
    if (key.length < 16 || key.length > 64) {
      authError = 'Administrator key must be between 16 and 64 characters';
      return;
    }
    authSubmitting = true;
    authError = '';
    try {
      await bootstrapAuth(nodeIp, key);
      const res = await createSession(nodeIp, key);
      nodeAuthStore.setSession({
        address: nodeIp,
        nodeId: liveState.nodeId || '',
        role: res.role,
        sessionToken: res.sessionToken,
        expiresAt: Date.now() + res.expiresIn * 1000,
      });
      bootstrapKeyInput = '';
    } catch (err) {
      authError = err instanceof Error ? err.message : String(err);
    } finally {
      authSubmitting = false;
    }
  }

  async function submitLogin() {
    if (!nodeIp) return;
    const key = loginKeyInput.trim();
    if (!key) {
      authError = 'Please enter your access key';
      return;
    }
    authSubmitting = true;
    authError = '';
    try {
      const res = await createSession(nodeIp, key);
      nodeAuthStore.setSession({
        address: nodeIp,
        nodeId: liveState.nodeId || '',
        role: res.role,
        sessionToken: res.sessionToken,
        expiresAt: Date.now() + res.expiresIn * 1000,
      });
      loginKeyInput = '';
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      authError = msg.includes('401') || msg.includes('invalid') ? 'Invalid credentials' : msg;
      loginKeyInput = '';
    } finally {
      authSubmitting = false;
    }
  }

  async function signOutNode() {
    if (nodeIp) {
      await deleteSession(nodeIp).catch(() => {});
      nodeAuthStore.clearSession(nodeIp);
    }
    currentSession = null;
    if (nodeIp) checkNodeAuth(nodeIp);
  }

  async function openAccessControl() {
    showAccessControl = true;
    credsLoading = true;
    credsError = '';
    credsSuccess = '';
    newKeyInput = '';
    keyCopied = false;
    try {
      credsStatus = await getCredentialsStatus(nodeIp);
    } catch (err) {
      credsError = err instanceof Error ? err.message : String(err);
    } finally {
      credsLoading = false;
    }
  }

  async function handleRotateKey() {
    if (!nodeIp || !newKeyInput.trim()) return;
    const key = newKeyInput.trim();
    if (key.length < 16 || key.length > 64) {
      credsError = 'Access key must be between 16 and 64 characters';
      return;
    }
    credsLoading = true;
    credsError = '';
    credsSuccess = '';
    try {
      await rotateCredential(nodeIp, rotateRole, key);
      credsSuccess = `Successfully rotated ${rotateRole} key.`;
      newKeyInput = '';
      keyCopied = false;
      if (rotateRole === 'admin') {
        // Admin rotation invalidates all sessions; require re-login
        setTimeout(() => {
          showAccessControl = false;
          nodeAuthStore.clearSession(nodeIp);
          checkNodeAuth(nodeIp);
        }, 1500);
      } else {
        credsStatus = await getCredentialsStatus(nodeIp);
      }
    } catch (err) {
      credsError = err instanceof Error ? err.message : String(err);
    } finally {
      credsLoading = false;
    }
  }

  async function handleDisableRole(role: 'installer' | 'operator') {
    if (!nodeIp) return;
    credsLoading = true;
    credsError = '';
    credsSuccess = '';
    try {
      await disableCredential(nodeIp, role);
      credsSuccess = `Disabled ${role} credential.`;
      credsStatus = await getCredentialsStatus(nodeIp);
    } catch (err) {
      credsError = err instanceof Error ? err.message : String(err);
    } finally {
      credsLoading = false;
    }
  }

  function copyKey(val: string) {
    if (typeof navigator !== 'undefined' && navigator.clipboard) {
      navigator.clipboard.writeText(val);
      keyCopied = true;
      setTimeout(() => { keyCopied = false; }, 2500);
    }
  }

  function refreshNodeList() {
    nodeList = getSavedNodes();
    const stored = getSelectedNodeIp();

    if (stored !== nodeIp) {
      nodeIp = stored;
      selectLiveNode(stored);
      checkNodeAuth(stored);
      return;
    }

    if (nodeIp && !nodeList.some(node => node.ip === nodeIp)) {
      const fallback = nodeList[0]?.ip ?? '';
      nodeIp = fallback;
      selectLiveNode(fallback);
      checkNodeAuth(fallback);
    }
  }

  function selectNode(ip: string) {
    nodeIp = ip;
    selectLiveNode(ip);
    checkNodeAuth(ip);
  }

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    nodeList = getSavedNodes();

    if (nodeIp) {
      checkNodeAuth(nodeIp);
      connect(nodeIp);
    }

    const unsubAuth = nodeAuthStore.subscribe(() => {
      if (nodeIp) {
        currentSession = nodeAuthStore.getSession(nodeIp);
      } else {
        currentSession = null;
      }
    });

    const unsubLive = getNodeLiveState().subscribe(val => {
      liveState = val;
    });

    const onRegistryChanged = () => refreshNodeList();
    window.addEventListener(NODE_REGISTRY_CHANGED_EVENT, onRegistryChanged);

    return () => {
      unsubAuth();
      unsubLive();
      window.removeEventListener(NODE_REGISTRY_CHANGED_EVENT, onRegistryChanged);
    };
  });

  type NavLink = {
    group: string;
    href: string;
    icon: string;
    label: string;
    adminOnly?: boolean;
  };

  const navLinks: NavLink[] = [
    { group: 'Overview', href: '/dashboard',              icon: '⬡',  label: 'Fleet' },
    { group: 'Overview', href: '/dashboard/sync',         icon: '⇄',  label: 'Sync' },
    { group: 'Create',   href: '/dashboard/scenes',       icon: '◆',  label: 'Scenes' },
    { group: 'Create',   href: '/dashboard/cues',         icon: '⏯',  label: 'Cues' },
    { group: 'Create',   href: '/dashboard/timeline',     icon: '▤',  label: 'Timeline' },
    { group: 'Create',   href: '/dashboard/schedule',     icon: '◷',  label: 'Schedule' },
    { group: 'Output',   href: '/dashboard/live',         icon: '⚡',  label: 'Live' },
    { group: 'Output',   href: '/dashboard/channels',     icon: '◈',  label: 'Channels' },
    { group: 'Output',   href: '/dashboard/visualizer',   icon: '▶',  label: 'Visualizer' },
    { group: 'Output',   href: '/dashboard/preview',      icon: '◉',  label: 'Preview' },
    { group: 'System',   href: '/dashboard/thermal',      icon: '⬥',  label: 'Thermal' },
    { group: 'System',   href: '/dashboard/uptime',       icon: '▣',  label: 'Uptime' },
    { group: 'System',   href: '/dashboard/diagnostics',  icon: '⚕',  label: 'Diagnostics' },
    { group: 'System',   href: '/dashboard/factory',      icon: '⚙',  label: 'Factory', adminOnly: true },
    { group: 'System',   href: '/dashboard/firmware',     icon: '⟳',  label: 'Firmware', adminOnly: true },
  ];

  const mobilePrimaryHrefs = new Set([
    '/dashboard',
    '/dashboard/scenes',
    '/dashboard/timeline',
    '/dashboard/live',
  ]);
  let visibleNavLinks = $derived(navLinks.filter(link => !link.adminOnly || isAdmin));
  let mobilePrimaryLinks = $derived(visibleNavLinks.filter(link => mobilePrimaryHrefs.has(link.href)));
  let mobileMoreLinks = $derived(visibleNavLinks.filter(link => !mobilePrimaryHrefs.has(link.href)));
  let routeRequiresAdmin = $derived(
    navLinks.some(link => link.href === $page.url.pathname && link.adminOnly)
  );

  $effect(() => {
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

{#if !authed}
<div class="pin-wall">
  <div class="pin-card">
    <div class="pin-logo">⬡</div>
    <h1>LumiGrid Controller</h1>

    {#if !nodeIp}
      <p class="pin-sub">Select or enter a target node to begin</p>
      <select class="pin-node-select" onchange={(e) => selectNode((e.target as HTMLSelectElement).value)}>
        <option value="">— Select Node —</option>
        {#each nodeList as node}
          <option value={node.ip}>{node.name === node.ip ? node.ip : `${node.name} (${node.ip})`}</option>
        {/each}
      </select>
    {:else if authLoading}
      <p class="pin-sub">Connecting to node {nodeIp}...</p>
      <div class="auth-spinner"></div>
    {:else if authStatusError}
      <p class="pin-sub">Node: <strong>{nodeIp}</strong></p>
      <p class="pin-error">{authStatusError}</p>
      <button class="pin-btn secondary" onclick={() => checkNodeAuth(nodeIp)}>Retry Connection</button>
    {:else if authStatus && !authStatus.provisioned}
      <!-- First-Boot Provisioning Surface -->
      <p class="pin-sub">Node <strong>{nodeIp}</strong> is unprovisioned</p>
      <p class="auth-desc">Provision an initial Administrator access key (16 to 64 characters) to commission this node.</p>
      
      <div class="key-input-row">
        <input
          type="text"
          placeholder="New Admin Key (min 16 chars)"
          class="pin-input text"
          bind:value={bootstrapKeyInput}
        />
        <button
          type="button"
          class="gen-key-btn"
          onclick={() => { bootstrapKeyInput = generateSecureKey(); }}
          title="Generate random secure key"
        >
          🎲 Generate
        </button>
      </div>

      {#if authError}<p class="pin-error">{authError}</p>{/if}
      <button
        class="pin-btn"
        disabled={authSubmitting || bootstrapKeyInput.length < 16}
        onclick={submitBootstrap}
      >
        {authSubmitting ? 'Provisioning...' : 'Provision & Sign In'}
      </button>
    {:else}
      <!-- Provisioned Node Login Surface -->
      <p class="pin-sub">Node <strong>{nodeIp}</strong></p>
      <p class="auth-desc">Enter your Administrator, Installer, or Operator access key.</p>
      
      <!-- svelte-ignore a11y_autofocus -->
      <input
        type="password"
        placeholder="Node Access Key"
        class="pin-input text"
        bind:value={loginKeyInput}
        onkeydown={(e) => e.key === 'Enter' && submitLogin()}
        autofocus
      />

      {#if authError}<p class="pin-error">{authError}</p>{/if}
      <button
        class="pin-btn"
        disabled={authSubmitting || !loginKeyInput.trim()}
        onclick={submitLogin}
      >
        {authSubmitting ? 'Authenticating...' : 'Sign In'}
      </button>
    {/if}

    {#if nodeIp && nodeList.length > 1}
      <div class="switch-node-row">
        <button class="text-link" onclick={() => selectNode('')}>Switch Node</button>
      </div>
    {/if}
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
        <option value="">{nodeList.length ? '— select node —' : '— no nodes —'}</option>
        {#each nodeList as node}
          <option value={node.ip}>
            {node.name === node.ip ? node.ip : `${node.name} — ${node.ip}`}
          </option>
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
      <a class="manage-fleet-link" href="/dashboard">Manage Fleet</a>
    </div>

    <ul class="nav-links" role="list" bind:this={navListEl}>
      {#each visibleNavLinks as { group, href, icon, label }, i}
        {#if i === 0 || visibleNavLinks[i - 1].group !== group}
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
      {isAdmin ? '◈ Admin' : userRole === 'installer' ? '⚙ Installer' : '◦ Operator'}
    </div>
    {#if isAdmin}
      <button class="access-ctrl-btn" onclick={openAccessControl} title="Access Control & Credentials">
        Access Control
      </button>
    {/if}
    <button class="logout-btn" onclick={signOutNode} title="Sign out of node">
      Sign Out
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
      <option value="">{nodeList.length ? 'Select node' : 'No nodes'}</option>
      {#each nodeList as node}
        <option value={node.ip}>
          {node.name === node.ip ? node.ip : `${node.name} — ${node.ip}`}
        </option>
      {/each}
    </select>
    <span class="mobile-status {liveState.connection}" title={liveState.connection}>
      <span class="status-dot"></span>
      <span>{liveState.connection === 'connected' ? 'Live' : liveState.connection === 'connecting' ? '…' : 'Off'}</span>
    </span>
    <button class="mobile-lock" onclick={signOutNode} title="Sign out" aria-label="Sign out">⌁</button>
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
      <div
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
      </div>
    </div>
  {/if}

  {#if showAccessControl}
    <div
      class="modal-backdrop"
      role="presentation"
      onclick={(e) => { if (e.target === e.currentTarget) showAccessControl = false; }}
    >
      <div class="modal-card" role="dialog" aria-modal="true" aria-labelledby="modal-title">
        <div class="modal-header">
          <h3 id="modal-title">Access Control — {nodeIp}</h3>
          <button class="modal-close" onclick={() => { showAccessControl = false; }}>✕</button>
        </div>

        {#if credsLoading}
          <div class="auth-spinner"></div>
        {:else}
          {#if credsError}<p class="pin-error">{credsError}</p>{/if}
          {#if credsSuccess}<p class="creds-success">{credsSuccess}</p>{/if}

          {#if credsStatus}
            <div class="creds-table">
              <div class="cred-row">
                <span class="cred-role">Administrator</span>
                <span class="cred-badge enabled">Configured</span>
              </div>
              <div class="cred-row">
                <span class="cred-role">Installer</span>
                <span class="cred-badge" class:enabled={credsStatus.installerConfigured}>
                  {credsStatus.installerConfigured ? 'Configured' : 'Disabled'}
                </span>
                {#if credsStatus.installerConfigured}
                  <button class="small-btn danger" onclick={() => handleDisableRole('installer')}>Disable</button>
                {/if}
              </div>
              <div class="cred-row">
                <span class="cred-role">Operator</span>
                <span class="cred-badge" class:enabled={credsStatus.operatorConfigured}>
                  {credsStatus.operatorConfigured ? 'Configured' : 'Disabled'}
                </span>
                {#if credsStatus.operatorConfigured}
                  <button class="small-btn danger" onclick={() => handleDisableRole('operator')}>Disable</button>
                {/if}
              </div>
            </div>
          {/if}

          <div class="rotate-section">
            <h4>Set / Rotate Credential</h4>
            <div class="rotate-role-select">
              <label>
                <input type="radio" name="rotateRole" value="admin" bind:group={rotateRole} /> Admin
              </label>
              <label>
                <input type="radio" name="rotateRole" value="installer" bind:group={rotateRole} /> Installer
              </label>
              <label>
                <input type="radio" name="rotateRole" value="operator" bind:group={rotateRole} /> Operator
              </label>
            </div>

            <div class="key-input-row">
              <input
                type="text"
                placeholder="New key (16-64 chars)"
                class="pin-input text"
                bind:value={newKeyInput}
              />
              <button
                type="button"
                class="gen-key-btn"
                onclick={() => { newKeyInput = generateSecureKey(); }}
              >
                🎲 Generate
              </button>
            </div>

            {#if newKeyInput}
              <div class="key-copy-row">
                <button type="button" class="small-btn" onclick={() => copyKey(newKeyInput)}>
                  {keyCopied ? '✓ Copied' : '📋 Copy Once'}
                </button>
                <small class="copy-note">Credentials will never be shown again after rotation.</small>
              </div>
            {/if}

            <button
              class="pin-btn"
              disabled={newKeyInput.length < 16}
              onclick={handleRotateKey}
            >
              Rotate {rotateRole} Key
            </button>
          </div>
        {/if}
      </div>
    </div>
  {/if}

  <main class="dash-main">
    {#if routeRequiresAdmin && !isAdmin}
      <section class="access-gate" aria-labelledby="access-gate-title">
        <div class="access-gate-icon" aria-hidden="true">◇</div>
        <h1 id="access-gate-title">Admin access required</h1>
        <p>This maintenance workspace requires Administrator privileges. Sign in with the node's Admin key to continue.</p>
        <a href="/dashboard" class="access-gate-link">Return to Fleet</a>
      </section>
    {:else}
      {@render children()}
    {/if}
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
  .pin-input.text {
    font-size: 1rem;
    letter-spacing: normal;
    text-align: left;
    font-family: inherit;
  }
  .pin-node-select {
    width: 100%;
    padding: 0.75rem 1rem;
    font-size: 0.95rem;
    background: rgba(255,255,255,.06);
    border: 1px solid rgba(255,255,255,.14);
    border-radius: 12px;
    color: #f5f5f7;
    margin-bottom: 1rem;
    box-sizing: border-box;
  }
  .auth-desc {
    color: #a1a1a6;
    font-size: 0.85rem;
    line-height: 1.45;
    margin: 0 0 1.25rem;
  }
  .key-input-row {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 0.75rem;
    align-items: stretch;
  }
  .key-input-row .pin-input {
    margin-bottom: 0;
    flex: 1;
  }
  .gen-key-btn {
    background: rgba(255,255,255,.08);
    border: 1px solid rgba(255,255,255,.16);
    border-radius: 12px;
    color: #f5f5f7;
    font-size: 0.8rem;
    font-weight: 600;
    padding: 0 0.85rem;
    white-space: nowrap;
    cursor: pointer;
    transition: background .18s;
  }
  .gen-key-btn:hover {
    background: rgba(255,255,255,.14);
  }
  .switch-node-row {
    margin-top: 1rem;
    border-top: 1px solid rgba(255,255,255,.08);
    padding-top: 0.75rem;
  }
  .text-link {
    background: none;
    border: none;
    color: #a1a1a6;
    font-size: 0.8rem;
    cursor: pointer;
    text-decoration: underline;
  }
  .text-link:hover { color: #f5f5f7; }
  .access-ctrl-btn {
    background: rgba(255,149,0,.10);
    border: 1px solid rgba(255,149,0,.25);
    border-radius: 8px;
    color: #ff9500;
    font-size: 0.75rem;
    font-weight: 600;
    padding: 0.35rem 0.5rem;
    cursor: pointer;
    transition: background .2s, border-color .2s;
  }
  .access-ctrl-btn:hover {
    background: rgba(255,149,0,.20);
    border-color: rgba(255,149,0,.4);
  }
  .modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0,0,0,.72);
    backdrop-filter: blur(10px);
    -webkit-backdrop-filter: blur(10px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
  }
  .modal-card {
    background: rgba(24,24,27,.96);
    border: 1px solid rgba(255,255,255,.14);
    border-radius: 20px;
    width: min(480px, 92vw);
    padding: 1.75rem;
    box-shadow: 0 24px 64px rgba(0,0,0,.7);
    color: #f5f5f7;
  }
  .modal-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 1.25rem;
  }
  .modal-header h3 {
    margin: 0;
    font-size: 1.15rem;
  }
  .modal-close {
    background: none;
    border: none;
    color: #a1a1a6;
    font-size: 1.1rem;
    cursor: pointer;
  }
  .modal-close:hover { color: #f5f5f7; }
  .creds-table {
    display: flex;
    flex-direction: column;
    gap: 0.65rem;
    margin-bottom: 1.25rem;
    background: rgba(255,255,255,.03);
    border: 1px solid rgba(255,255,255,.07);
    border-radius: 12px;
    padding: 0.85rem 1rem;
  }
  .cred-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 0.88rem;
  }
  .cred-badge {
    font-size: 0.72rem;
    font-weight: 600;
    padding: 0.2rem 0.55rem;
    border-radius: 999px;
    background: rgba(255,255,255,.06);
    color: #8e8e93;
  }
  .cred-badge.enabled {
    background: rgba(52,199,89,.15);
    color: #34c759;
  }
  .rotate-section {
    border-top: 1px solid rgba(255,255,255,.08);
    padding-top: 1.25rem;
  }
  .rotate-section h4 {
    margin: 0 0 0.85rem;
    font-size: 0.95rem;
  }
  .rotate-role-select {
    display: flex;
    gap: 1.25rem;
    margin-bottom: 1rem;
    font-size: 0.85rem;
    color: #a1a1a6;
  }
  .rotate-role-select label {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    cursor: pointer;
  }
  .key-copy-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    margin-bottom: 1rem;
  }
  .copy-note {
    color: #8e8e93;
    font-size: 0.75rem;
  }
  .small-btn {
    background: rgba(255,255,255,.08);
    border: 1px solid rgba(255,255,255,.16);
    border-radius: 6px;
    color: #f5f5f7;
    font-size: 0.75rem;
    padding: 0.25rem 0.6rem;
    cursor: pointer;
  }
  .small-btn:hover { background: rgba(255,255,255,.14); }
  .small-btn.danger {
    border-color: rgba(255,69,58,.35);
    color: #ff453a;
    background: rgba(255,69,58,.08);
  }
  .small-btn.danger:hover { background: rgba(255,69,58,.16); }
  .creds-success {
    color: #34c759;
    font-size: 0.85rem;
    margin-bottom: 0.75rem;
  }
  .auth-spinner {
    width: 28px;
    height: 28px;
    border: 2px solid rgba(255,255,255,.1);
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin: 1.5rem auto;
  }
  @keyframes spin {
    to { transform: rotate(360deg); }
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
    margin-bottom: 0.5rem;
  }
  .pin-btn:hover:not(:disabled) {
    background: #ff3b30;
    transform: translateY(-1px);
    box-shadow: 0 8px 24px rgba(255,45,85,.55);
  }
  .pin-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
    box-shadow: none;
  }
  .pin-btn.secondary {
    background: rgba(255,255,255,.08);
    border: 1px solid rgba(255,255,255,.16);
    box-shadow: none;
  }
  .pin-btn.secondary:hover:not(:disabled) {
    background: rgba(255,255,255,.14);
  }

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
  .manage-fleet-link {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    min-height: 30px;
    padding: .35rem .55rem;
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 8px;
    background: rgba(255,255,255,.035);
    color: #8e8e93;
    text-decoration: none;
    font-size: .72rem;
    font-weight: 650;
    transition: background .18s ease, border-color .18s ease, color .18s ease;
  }
  .manage-fleet-link:hover {
    background: rgba(255,255,255,.065);
    border-color: rgba(255,255,255,.14);
    color: #f5f5f7;
  }

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
  .access-gate {
    width: min(520px, 100%);
    min-height: 320px;
    margin: clamp(2rem, 8vh, 6rem) auto;
    display: grid;
    place-items: center;
    align-content: center;
    gap: .8rem;
    padding: 2rem;
    text-align: center;
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-xl);
    box-shadow: var(--dash-shadow);
  }
  .access-gate-icon {
    width: 48px;
    height: 48px;
    display: grid;
    place-items: center;
    border-radius: 15px;
    background: color-mix(in oklab, var(--dash-warm) 12%, transparent);
    border: 1px solid color-mix(in oklab, var(--dash-warm) 26%, transparent);
    color: var(--dash-warm);
    font-size: 1.25rem;
  }
  .access-gate h1 { margin: .2rem 0 0; font-size: 1.2rem; color: #fff; }
  .access-gate p { max-width: 52ch; margin: 0; color: #8e8e93; font-size: .86rem; line-height: 1.55; }
  .access-gate-link {
    margin-top: .35rem;
    padding: .55rem .85rem;
    border-radius: 10px;
    background: rgba(var(--dash-accent-rgb),.13);
    border: 1px solid rgba(var(--dash-accent-rgb),.25);
    color: var(--dash-accent);
    font-size: .8rem;
    font-weight: 700;
    text-decoration: none;
  }

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
      touch-action: manipulation;
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
      overscroll-behavior: contain;
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
