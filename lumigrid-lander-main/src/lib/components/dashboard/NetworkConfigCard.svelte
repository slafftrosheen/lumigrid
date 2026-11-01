<script lang="ts">
  import { onMount, untrack } from 'svelte';
  import { nodeAuthStore, type NodeAuthSession } from '$lib/auth/nodeAuth';
  import {
    getNetworkConfig, saveNetworkConfig, getNetworkStatus,
    type NetworkConfigWire, type NetworkConfigUpdate,
    type NetworkRuntimeStatus, type NetworkMode
  } from '$lib/api/config';

  interface Props {
    nodeIp: string;
    isAdmin?: boolean;
    onNetworkChanged?: () => void;
  }
  let { nodeIp, isAdmin = true, onNetworkChanged }: Props = $props();
  let loading = $state(true);
  let applying = $state(false);
  let config = $state<NetworkConfigWire | null>(null);
  let configLoaded = $state(false);
  let status = $state<NetworkRuntimeStatus | null>(null);
  let loadError = $state('');
  let statusError = $state('');
  let mode = $state<NetworkMode>('sta_with_ap_fallback');
  let ssid = $state('');
  let password = $state('');
  let clearPassword = $state(false);
  let apName = $state('');
  let apChannel = $state(1);
  let validationError = $state<string | null>(null);
  let applyMessage = $state<string | null>(null);
  let applyError = $state<string | null>(null);
  let authRevision = $state(0);
  let activeIp = '';
  let activeSession: NodeAuthSession | null = null;
  let generation = 0;
  let loadRequest = 0;
  let statusRequest = 0;
  let applyRequest = 0;
  let disposed = false;
  let pollTimer: ReturnType<typeof setTimeout> | null = null;
  let deadlineTimer: ReturnType<typeof setTimeout> | null = null;
  type Draft = { mode: NetworkMode; ssid: string; password: string; clearPassword: boolean; apName: string; apChannel: number };
  const drafts = new Map<string, { session: NodeAuthSession | null; values: Draft }>();
  let inputsDisabled = $derived(loading || applying || !configLoaded || !isAdmin);

  const draft = (): Draft => ({ mode, ssid, password, clearPassword, apName, apChannel });
  function restoreDraft(value: Draft) {
    mode = value.mode; ssid = value.ssid; password = value.password;
    clearPassword = value.clearPassword; apName = value.apName; apChannel = value.apChannel;
  }
  function savedDraft(cfg: NetworkConfigWire): Draft {
    return { mode: cfg.mode, ssid: cfg.ssid || '', password: '', clearPassword: false,
      apName: cfg.apName || '', apChannel: cfg.apChannel ?? 1 };
  }
  function captureOwner(ip = activeIp) {
    const epoch = generation;
    const session = activeSession;
    return () => !disposed && epoch === generation && ip === nodeIp &&
      nodeAuthStore.getSession(ip) === session;
  }
  function stopPolling() {
    if (pollTimer) clearTimeout(pollTimer);
    if (deadlineTimer) clearTimeout(deadlineTimer);
    pollTimer = deadlineTimer = null;
  }

  let connectionLabel = $derived.by(() => {
    if (loading) return 'Checking…';
    if (statusError) return status ? 'Stale' : 'Unavailable';
    if (!status) return 'Unavailable';
    if (status.state === 'fallback_ap') return 'Recovery AP';
    if (status.staConnected) return 'Connected';
    if (status.state === 'connecting') return 'Connecting';
    if (status.apActive) return 'AP Active';
    return status.state.toUpperCase();
  });
  let connectionClass = $derived.by(() => {
    if (!status || statusError) return 'offline';
    if (status.state === 'fallback_ap') return 'recovery';
    if (status.staConnected) return 'connected';
    if (status.state === 'connecting') return 'connecting';
    if (status.apActive) return 'ap-active';
    return 'offline';
  });

  async function loadData(preserved?: Draft) {
    const ip = activeIp;
    const isCurrent = captureOwner(ip);
    const request = ++loadRequest;
    if (!ip) { loading = false; return; }
    loading = true;
    statusRequest++;
    loadError = statusError = '';
    const [cfg, st] = await Promise.allSettled([getNetworkConfig(ip), getNetworkStatus(ip)]);
    if (!isCurrent() || request !== loadRequest) return;
    if (cfg.status === 'fulfilled') {
      config = cfg.value;
      configLoaded = true;
      restoreDraft(preserved ?? savedDraft(cfg.value));
    } else {
      configLoaded = false;
      loadError = 'Saved network settings are unavailable. Load them before editing or applying changes.';
    }
    if (st.status === 'fulfilled') status = st.value;
    else statusError = 'Network status is unavailable. Any values shown are the last received from this node.';
    loading = false;
  }

  $effect(() => {
    const ip = nodeIp;
    authRevision;
    untrack(() => {
      const session = nodeAuthStore.getSession(ip);
      if (activeIp === ip && activeSession === session) return;
      if (activeIp && config) drafts.set(activeIp, { session: activeSession, values: draft() });
      generation++;
      stopPolling();
      activeIp = ip;
      activeSession = session;
      config = status = null;
      configLoaded = false;
      applying = false;
      applyMessage = applyError = validationError = null;
      loadError = statusError = '';
      restoreDraft({ mode: 'sta_with_ap_fallback', ssid: '', password: '', clearPassword: false, apName: '', apChannel: 1 });
      const saved = drafts.get(ip);
      void loadData(saved?.session === session ? saved.values : undefined);
    });
  });

  onMount(() => {
    disposed = false;
    const unsubscribe = nodeAuthStore.subscribe(() => { authRevision++; });
    const statusTimer = setInterval(async () => {
      if (loading || applying || !activeIp) return;
      const ip = activeIp;
      const isCurrent = captureOwner(ip);
      const request = ++statusRequest;
      try {
        const current = await getNetworkStatus(ip);
        if (!isCurrent() || applying || request !== statusRequest) return;
        status = current;
        statusError = '';
      } catch {
        if (isCurrent() && !applying && request === statusRequest) {
          statusError = 'Network status is unavailable. Any values shown are the last received from this node.';
        }
      }
    }, 10000);
    return () => {
      disposed = true; generation++; stopPolling(); clearInterval(statusTimer);
      unsubscribe(); drafts.clear();
    };
  });

  function validateInput(): string | null {
    if (mode === 'sta_only' || mode === 'sta_with_ap_fallback') {
      if (!ssid.trim()) return 'Wi-Fi Network SSID is required for Station modes.';
      if (ssid.length > 32) return 'Wi-Fi Network SSID cannot exceed 32 characters.';
    }
    if (password.length > 0 && (password.length < 8 || password.length > 63)) {
      return 'Wi-Fi password must be between 8 and 63 characters.';
    }
    if (!Number.isInteger(apChannel) || apChannel < 1 || apChannel > 13) {
      return 'AP Channel must be a whole number between 1 and 13.';
    }
    if (apName.length > 32) return 'AP Name cannot exceed 32 characters.';
    return null;
  }

  async function handleApply() {
    if (inputsDisabled) return;
    const ip = activeIp;
    const ownsNode = captureOwner(ip);
    const request = ++applyRequest;
    const isCurrent = () => ownsNode() && request === applyRequest;
    if (!isCurrent()) return;
    validationError = validateInput();
    if (validationError) return;
    applyError = null;
    statusRequest++;
    applying = true;
    applyMessage = 'Sending network settings…';
    const update: NetworkConfigUpdate = { mode, ssid: ssid.trim(), apChannel };
    if (apName.trim()) update.apName = apName.trim();
    if (clearPassword) update.clearPassword = true;
    else if (password) update.password = password;
    try {
      const result = await saveNetworkConfig(ip, update);
      if (!isCurrent()) return;
      if (!result.accepted) throw new Error('The node did not accept these network settings.');
      applyMessage = 'Settings accepted. Waiting for the node to confirm its network state…';
      startReconnectPolling(ip, isCurrent, update, result.commandId);
    } catch (err: unknown) {
      if (!isCurrent()) return;
      applyRequest++;
      applying = false;
      applyMessage = null;
      const message = err instanceof Error ? err.message : String(err);
      applyError = message.includes('network_apply_in_progress') || message.includes('HTTP 409')
        ? 'Another network apply operation is already in progress. Your edits are preserved.'
        : `Network settings were not confirmed. Your edits are preserved. ${message}`;
    }
  }

  function matchesSaved(cfg: NetworkConfigWire, update: NetworkConfigUpdate) {
    return cfg.mode === update.mode && (cfg.ssid || '') === update.ssid &&
      (cfg.apChannel ?? 1) === update.apChannel &&
      (update.apName === undefined || cfg.apName === update.apName) &&
      (!update.clearPassword || !cfg.passwordSet) && (!update.password || cfg.passwordSet);
  }

  function startReconnectPolling(ip: string, isCurrent: () => boolean, update: NetworkConfigUpdate, commandId?: number) {
    stopPolling();
    const finish = (error: string | null) => {
      if (!isCurrent()) return;
      stopPolling();
      applyRequest++;
      applying = false;
      applyMessage = null;
      applyError = error;
    };
    deadlineTimer = setTimeout(() => {
      finish('Network settings are still unconfirmed after 60 seconds. Your edits are preserved. Check this node’s connection before retrying.');
    }, 60000);
    const poll = async () => {
      if (!isCurrent() || !applying) return;
      try {
        const st = await getNetworkStatus(ip);
        if (!isCurrent() || !applying) return;
        status = st;
        statusError = '';
        if (st.pendingCommandId && commandId && st.pendingCommandId !== commandId) {
          finish('A different network operation is active on this node. Your edits are preserved.');
          return;
        }
        if (['applying', 'waiting_for_network', 'rolling_back'].includes(st.applyState)) {
          pollTimer = setTimeout(poll, 2000);
          return;
        }
        if (st.applyState === 'failed' || st.lastApplyError) {
          finish(`The node could not apply these settings (${st.lastApplyError || st.applyState}). Your edits are preserved.`);
          return;
        }
        const cfg = await getNetworkConfig(ip);
        if (!isCurrent() || !applying) return;
        config = cfg;
        if (!st.persistedMatchesRuntime || !matchesSaved(cfg, update)) {
          finish('The node’s saved and running settings do not confirm this request. Your edits are preserved.');
          return;
        }
        restoreDraft(savedDraft(cfg));
        finish(null);
        applyMessage = 'Node reports matching saved and running network settings.';
        onNetworkChanged?.();
      } catch {
        if (!isCurrent() || !applying) return;
        statusError = 'Waiting for current network status from this node.';
        pollTimer = setTimeout(poll, 2000);
      }
    };
    pollTimer = setTimeout(poll, 2000);
  }
</script>

<div class="network-card">
  <div class="card-header">
    <div class="header-left">
      <h3 class="card-title">Network Configuration</h3>
      <span class="sub-label">Wi-Fi & Recovery Access Point · {nodeIp}</span>
    </div>
    <div class="header-right">
      <span class="connection-pill {connectionClass}">
        <span class="dot"></span>
        {connectionLabel}
      </span>
    </div>
  </div>

  {#if applying}
    <div class="applying-banner" role="status" aria-live="polite">
      <div class="spinner"></div>
      <div class="banner-text">
        <strong>{applyMessage}</strong>
        <p class="banner-sub">Keep this node selected while its network change is being confirmed.</p>
      </div>
    </div>
  {/if}

  {#if applyError}
    <div class="error-banner" role="alert">
      <span class="error-icon">⚠</span>
      <span class="error-text">{applyError}</span>
    </div>
  {/if}

  {#if loadError || statusError}
    <div class="error-banner" role="status">
      <span class="error-text">{loadError || statusError}</span>
    </div>
  {/if}
  {#if !applying && applyMessage}
    <p class="field-hint" role="status">{applyMessage}</p>
  {/if}

  {#if validationError}
    <div class="validation-banner" role="alert">
      <span>{validationError}</span>
    </div>
  {/if}

  <div class="network-body">
    <!-- Network Mode -->
    <div class="form-group">
      <span class="form-label">Operating Mode</span>
      <div class="mode-options">
        <label class="mode-btn" class:active={mode === 'sta_with_ap_fallback'}>
          <input
            type="radio"
            name="network_mode"
            value="sta_with_ap_fallback"
            bind:group={mode}
            disabled={inputsDisabled}
          />
          <span class="mode-text">
            <strong>Join Wi-Fi with Recovery AP</strong>
            <small>Connects to LAN; activates recovery AP if connection fails</small>
          </span>
        </label>

        <label class="mode-btn" class:active={mode === 'ap_only'}>
          <input
            type="radio"
            name="network_mode"
            value="ap_only"
            bind:group={mode}
            disabled={inputsDisabled}
          />
          <span class="mode-text">
            <strong>Access Point Only</strong>
            <small>Standalone SoftAP (192.168.4.1)</small>
          </span>
        </label>

        <label class="mode-btn" class:active={mode === 'sta_only'}>
          <input
            type="radio"
            name="network_mode"
            value="sta_only"
            bind:group={mode}
            disabled={inputsDisabled}
          />
          <span class="mode-text">
            <strong>Wi-Fi Only</strong>
            <small>LAN connection only (SoftAP disabled)</small>
          </span>
        </label>
      </div>
    </div>

    <!-- Wi-Fi Network (SSID / Password) -->
    {#if mode !== 'ap_only'}
      <div class="section-divider"></div>
      <div class="grid-2-col">
        <div class="form-group">
          <label class="form-label" for="net-ssid">Wi-Fi Network SSID</label>
          <input
            id="net-ssid"
            class="input-field"
            type="text"
            placeholder="e.g. Venue-Lighting-2.4G"
            maxlength={32}
            bind:value={ssid}
            disabled={inputsDisabled}
          />
        </div>

        <div class="form-group">
          <div class="label-row">
            <label class="form-label" for="net-pwd">Wi-Fi Password</label>
            {#if config?.passwordSet}
              <span class="configured-badge">● Password configured</span>
            {/if}
          </div>
          <input
            id="net-pwd"
            class="input-field"
            type="password"
            placeholder={config?.passwordSet ? 'Leave blank to preserve' : 'Enter WPA/WPA2 password'}
            minlength={8}
            maxlength={63}
            bind:value={password}
            disabled={inputsDisabled || clearPassword}
          />
          {#if config?.passwordSet}
            <label class="checkbox-label">
              <input
                type="checkbox"
                bind:checked={clearPassword}
                disabled={inputsDisabled}
              />
              <span>Clear saved password (open network)</span>
            </label>
          {/if}
        </div>
      </div>
    {/if}

    <!-- Recovery AP Settings -->
    {#if mode !== 'sta_only'}
      <div class="section-divider"></div>
      <div class="grid-2-col">
        <div class="form-group">
          <label class="form-label" for="net-ap-name">Recovery AP Name</label>
          <input
            id="net-ap-name"
            class="input-field"
            type="text"
            placeholder="LumiGrid-Recovery"
            maxlength={32}
            bind:value={apName}
            disabled={inputsDisabled}
          />
          <span class="field-hint">Defaults to node identifier if left blank.</span>
        </div>

        <div class="form-group">
          <label class="form-label" for="net-ap-ch">AP Wi-Fi Channel</label>
          <input
            id="net-ap-ch"
            class="input-field"
            type="number"
            min={1}
            max={13}
            bind:value={apChannel}
            disabled={inputsDisabled}
          />
          <span class="field-hint">Channels 1–13 (2.4 GHz).</span>
        </div>
      </div>
    {/if}

    <!-- Runtime Telemetry Strip -->
    {#if status}
      <div class="runtime-telemetry">
        <div class="telem-item">
          <span class="telem-label">Station IP</span>
          <span class="telem-val mono">{status.staConnected ? status.staIp || '—' : '—'}</span>
        </div>
        <div class="telem-item">
          <span class="telem-label">AP IP</span>
          <span class="telem-val mono">{status.apActive ? status.apIp || '—' : '—'}</span>
        </div>
        <div class="telem-item">
          <span class="telem-label">Signal RSSI</span>
          <span class="telem-val">{status.staConnected ? `${status.staRssi} dBm` : '—'}</span>
        </div>
        <div class="telem-item">
          <span class="telem-label">Retry State</span>
          <span class="telem-val">{status.retryCount > 0 ? `${status.retryCount} retries` : 'Clean'}</span>
        </div>
      </div>
    {/if}

    <!-- Action Bar -->
    <div class="card-footer">
      {#if loadError || statusError}
        <button class="btn-apply" type="button" disabled={loading || applying}
          onclick={() => loadData(config ? draft() : undefined)}>
          {loading ? 'Loading…' : 'Retry Read'}
        </button>
      {/if}
      <button
        class="btn-apply"
        type="button"
        onclick={handleApply}
        disabled={inputsDisabled}
      >
        {#if loading}
          Loading Saved Settings…
        {:else if applying}
          Applying…
        {:else}
          Apply Network Settings
        {/if}
      </button>
    </div>
  </div>
</div>

<style>
  .network-card {
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    padding: 1.25rem;
    margin-top: 1.25rem;
    color: #f5f5f7;
  }

  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1.25rem;
  }

  .card-title {
    font-size: 1.05rem;
    font-weight: 600;
    margin: 0;
    color: #f5f5f7;
  }

  .sub-label {
    font-size: 0.78rem;
    color: #86868b;
  }

  .connection-pill {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0.25rem 0.65rem;
    border-radius: 999px;
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }

  .connection-pill .dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
  }

  .connection-pill.connected {
    background: rgba(34, 197, 94, 0.12);
    color: #22c55e;
  }
  .connection-pill.connected .dot {
    background: #22c55e;
  }

  .connection-pill.connecting {
    background: rgba(234, 179, 8, 0.12);
    color: #eab308;
  }
  .connection-pill.connecting .dot {
    background: #eab308;
  }

  .connection-pill.recovery {
    background: rgba(249, 115, 22, 0.12);
    color: #f97316;
  }
  .connection-pill.recovery .dot {
    background: #f97316;
  }

  .connection-pill.ap-active {
    background: rgba(168, 85, 247, 0.12);
    color: #a855f7;
  }
  .connection-pill.ap-active .dot {
    background: #a855f7;
  }

  .connection-pill.offline {
    background: rgba(239, 68, 68, 0.12);
    color: #ef4444;
  }
  .connection-pill.offline .dot {
    background: #ef4444;
  }

  .applying-banner {
    display: flex;
    align-items: center;
    gap: 0.85rem;
    background: rgba(234, 179, 8, 0.1);
    border: 1px solid rgba(234, 179, 8, 0.25);
    border-radius: 8px;
    padding: 0.85rem 1rem;
    margin-bottom: 1.25rem;
    color: #fef08a;
  }

  .banner-text strong {
    display: block;
    font-size: 0.88rem;
  }

  .banner-sub {
    font-size: 0.76rem;
    margin: 0.2rem 0 0;
    color: #fef9c3;
    opacity: 0.85;
  }

  .spinner {
    width: 20px;
    height: 20px;
    border: 2px solid rgba(234, 179, 8, 0.3);
    border-top-color: #eab308;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }

  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  .error-banner {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    background: rgba(239, 68, 68, 0.1);
    border: 1px solid rgba(239, 68, 68, 0.3);
    border-radius: 8px;
    padding: 0.75rem 1rem;
    margin-bottom: 1.25rem;
    color: #fca5a5;
    font-size: 0.84rem;
  }

  .validation-banner {
    background: rgba(249, 115, 22, 0.1);
    border: 1px solid rgba(249, 115, 22, 0.3);
    border-radius: 8px;
    padding: 0.6rem 0.85rem;
    margin-bottom: 1rem;
    color: #fdba74;
    font-size: 0.82rem;
  }

  .mode-options {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 0.75rem;
    margin-top: 0.4rem;
  }

  .mode-btn {
    display: flex;
    align-items: flex-start;
    gap: 0.6rem;
    padding: 0.75rem;
    border-radius: 8px;
    border: 1px solid rgba(255, 255, 255, 0.1);
    background: rgba(255, 255, 255, 0.02);
    cursor: pointer;
    transition: border-color 0.15s, background 0.15s;
  }

  .mode-btn.active {
    border-color: rgba(255, 255, 255, 0.3);
    background: rgba(255, 255, 255, 0.06);
  }

  .mode-btn input[type="radio"] {
    margin-top: 0.2rem;
  }

  .mode-text strong {
    display: block;
    font-size: 0.84rem;
    color: #f5f5f7;
  }

  .mode-text small {
    display: block;
    font-size: 0.74rem;
    color: #86868b;
    margin-top: 0.15rem;
    line-height: 1.3;
  }

  .section-divider {
    height: 1px;
    background: rgba(255, 255, 255, 0.06);
    margin: 1.25rem 0;
  }

  .grid-2-col {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    gap: 1rem;
  }

  .form-group {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }

  .form-label {
    font-size: 0.82rem;
    color: #a1a1a6;
    font-weight: 500;
  }

  .label-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .configured-badge {
    font-size: 0.72rem;
    color: #22c55e;
    font-weight: 500;
  }

  .input-field {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: 0.85rem;
    padding: 0.5rem 0.75rem;
    outline: none;
    transition: border-color 0.15s;
  }

  .input-field:focus {
    border-color: rgba(255, 255, 255, 0.35);
  }

  .input-field:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .field-hint {
    font-size: 0.72rem;
    color: #86868b;
  }

  .checkbox-label {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    margin-top: 0.35rem;
    font-size: 0.76rem;
    color: #86868b;
    cursor: pointer;
  }

  .runtime-telemetry {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
    gap: 0.75rem;
    background: rgba(0, 0, 0, 0.2);
    border-radius: 8px;
    padding: 0.75rem 1rem;
    margin-top: 1.25rem;
  }

  .telem-item {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
  }

  .telem-label {
    font-size: 0.72rem;
    color: #86868b;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .telem-val {
    font-size: 0.85rem;
    font-weight: 600;
    color: #e5e5ea;
  }

  .mono {
    font-family: monospace;
  }

  .card-footer {
    display: flex;
    flex-wrap: wrap;
    gap: var(--spacing-mobile-xs);
    justify-content: flex-end;
    margin-top: 1.25rem;
  }

  .btn-apply {
    background: var(--dash-accent);
    color: var(--dash-text);
    border: none;
    border-radius: 8px;
    padding: 0.55rem 1.4rem;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    transition: opacity 0.15s, background 0.15s;
  }

  .btn-apply:hover:not(:disabled) {
    background: var(--dash-accent-hover);
  }

  .btn-apply:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
</style>
