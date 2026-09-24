<script lang="ts">
  import { onMount } from 'svelte';
  import {
    getNetworkConfig,
    saveNetworkConfig,
    getNetworkStatus,
    type NetworkConfigWire,
    type NetworkConfigUpdate,
    type NetworkRuntimeStatus,
    type NetworkMode
  } from '$lib/api/config';

  interface Props {
    nodeIp: string;
    isAdmin?: boolean;
    onNetworkChanged?: () => void;
  }

  let { nodeIp, isAdmin = true, onNetworkChanged }: Props = $props();

  let loading = $state(true);
  let applying = $state(false);
  let pollInterval: ReturnType<typeof setInterval> | null = null;

  let config = $state<NetworkConfigWire | null>(null);
  let status = $state<NetworkRuntimeStatus | null>(null);

  // Form inputs
  let mode = $state<NetworkMode>('sta_with_ap_fallback');
  let ssid = $state('');
  let password = $state('');
  let clearPassword = $state(false);
  let apName = $state('');
  let apChannel = $state(1);

  // Error & notice states
  let validationError = $state<string | null>(null);
  let applyMessage = $state<string | null>(null);
  let applyError = $state<string | null>(null);

  // Connection badge derived label & class
  let connectionLabel = $derived.by(() => {
    if (!status && loading) return 'Checking…';
    if (!status) return 'Offline';
    if (status.state === 'fallback_ap') return 'Recovery AP';
    if (status.staConnected) return 'Connected';
    if (status.state === 'connecting') return 'Connecting';
    if (status.apActive) return 'AP Active';
    return status.state.toUpperCase();
  });

  let connectionClass = $derived.by(() => {
    if (!status) return 'offline';
    if (status.state === 'fallback_ap') return 'recovery';
    if (status.staConnected) return 'connected';
    if (status.state === 'connecting') return 'connecting';
    if (status.apActive) return 'ap-active';
    return 'offline';
  });

  async function loadData() {
    try {
      loading = true;
      const [cfg, st] = await Promise.all([
        getNetworkConfig(nodeIp).catch(() => null),
        getNetworkStatus(nodeIp).catch(() => null),
      ]);
      if (cfg) {
        config = cfg;
        mode = cfg.mode;
        ssid = cfg.ssid || '';
        apName = cfg.apName || '';
        apChannel = cfg.apChannel ?? 1;
        password = '';
        clearPassword = false;
      }
      if (st) {
        status = st;
      }
    } finally {
      loading = false;
    }
  }

  onMount(() => {
    loadData();
    return () => {
      if (pollInterval) clearInterval(pollInterval);
    };
  });

  function validateInput(): string | null {
    if (mode === 'sta_only' || mode === 'sta_with_ap_fallback') {
      if (!ssid || ssid.trim().length === 0) {
        return 'Wi-Fi Network SSID is required for Station modes.';
      }
      if (ssid.length > 32) {
        return 'Wi-Fi Network SSID cannot exceed 32 characters.';
      }
    }
    if (password.length > 0) {
      if (password.length < 8 || password.length > 63) {
        return 'Wi-Fi password must be between 8 and 63 characters.';
      }
    }
    if (apChannel < 1 || apChannel > 13) {
      return 'AP Channel must be between 1 and 13.';
    }
    if (apName && apName.length > 32) {
      return 'AP Name cannot exceed 32 characters.';
    }
    return null;
  }

  async function handleApply() {
    if (applying) return; // Prevent double apply

    validationError = validateInput();
    if (validationError) return;

    applyError = null;
    validationError = null;
    applying = true;
    applyMessage = 'Applying network settings… Connection may move to the new network. Reconnecting…';

    const update: NetworkConfigUpdate = {
      mode,
      ssid: ssid.trim(),
      apChannel,
    };

    if (apName.trim().length > 0) {
      update.apName = apName.trim();
    }

    if (clearPassword) {
      update.clearPassword = true;
    } else if (password.length > 0) {
      update.password = password;
    }

    try {
      await saveNetworkConfig(nodeIp, update);
    } catch (err: any) {
      if (err?.message?.includes('network_apply_in_progress') || err?.status === 409) {
        applyError = 'Another network apply operation is already in progress.';
        applying = false;
        applyMessage = null;
        return;
      }
      // If network dropped immediately or call failed, still proceed to poll
    }

    // Begin reconnect polling
    startReconnectPolling();
  }

  function startReconnectPolling() {
    if (pollInterval) clearInterval(pollInterval);
    let attempts = 0;
    const maxAttempts = 30; // 30 * 2s = 60s total polling window

    pollInterval = setInterval(async () => {
      attempts++;
      try {
        const st = await getNetworkStatus(nodeIp);
        if (st) {
          status = st;

          // If still transitioning
          if (st.applyState === 'applying' || st.applyState === 'waiting_for_network' || st.applyState === 'rolling_back') {
            return;
          }

          // Arrived at a terminal state
          clearInterval(pollInterval!);
          pollInterval = null;
          applying = false;
          applyMessage = null;

          // Reload fresh config
          const cfg = await getNetworkConfig(nodeIp).catch(() => null);
          if (cfg) {
            config = cfg;
            password = '';
            clearPassword = false;
          }

          if (st.state === 'fallback_ap' || st.lastApplyError === 'sta_connect_failed') {
            applyError = `Could not join "${st.staSsid || ssid}". Recovery access point is active. Check the Wi-Fi password and try again.`;
          } else if (st.persistedMatchesRuntime) {
            applyError = null;
            if (onNetworkChanged) onNetworkChanged();
          }
        }
      } catch {
        // Device is currently rebooting/switching Wi-Fi interfaces, keep waiting
        if (attempts >= maxAttempts) {
          clearInterval(pollInterval!);
          pollInterval = null;
          applying = false;
          applyMessage = null;
          applyError = 'Reconnection timed out. Please check your network connection and retry.';
        }
      }
    }, 2000);
  }
</script>

<div class="network-card">
  <div class="card-header">
    <div class="header-left">
      <h3 class="card-title">Network Configuration</h3>
      <span class="sub-label">Wi-Fi & Recovery Access Point Lifecycle</span>
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
        <p class="banner-sub">Firmware is safely reconfiguring network listeners without disrupting lighting output.</p>
      </div>
    </div>
  {/if}

  {#if applyError}
    <div class="error-banner" role="alert">
      <span class="error-icon">⚠</span>
      <span class="error-text">{applyError}</span>
    </div>
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
            disabled={applying || !isAdmin}
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
            disabled={applying || !isAdmin}
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
            disabled={applying || !isAdmin}
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
            disabled={applying || !isAdmin}
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
            disabled={applying || clearPassword || !isAdmin}
          />
          {#if config?.passwordSet}
            <label class="checkbox-label">
              <input
                type="checkbox"
                bind:checked={clearPassword}
                disabled={applying || !isAdmin}
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
            disabled={applying || !isAdmin}
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
            disabled={applying || !isAdmin}
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
          <span class="telem-val mono">{status.staIp || '0.0.0.0'}</span>
        </div>
        <div class="telem-item">
          <span class="telem-label">AP IP</span>
          <span class="telem-val mono">{status.apIp || '0.0.0.0'}</span>
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
      <button
        class="btn-apply"
        type="button"
        onclick={handleApply}
        disabled={applying || !isAdmin}
      >
        {#if applying}
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
    justify-content: flex-end;
    margin-top: 1.25rem;
  }

  .btn-apply {
    background: #d4af37;
    color: #000;
    border: none;
    border-radius: 8px;
    padding: 0.55rem 1.4rem;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    transition: opacity 0.15s, background 0.15s;
  }

  .btn-apply:hover:not(:disabled) {
    background: #e5be42;
  }

  .btn-apply:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
</style>
