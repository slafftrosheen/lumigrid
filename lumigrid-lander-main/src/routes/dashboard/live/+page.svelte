<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import {
    putManualState,
    releaseManualState,
    captureLiveDeskToScene,
    expandChannelMask,
    buildManualSnapshot,
    channelsToMask,
    type ManualChannelValue,
    type ManualAddressableValue,
    type ManualPwmValue,
    type ManualDeskChannel,
  } from '$lib/api/manual';
  import { getDeviceConfig, type DeviceConfig, type ChannelGroup } from '$lib/api/config';
  import { getPresets, savePreset, MAX_PRESETS, type ScenePreset } from '$lib/api/scenes';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';
  import { ManualSession, type ManualSessionSnapshot, type ManualSessionState } from '$lib/live/manualSession';

  interface ChannelDeskState {
    index: number;
    name: string;
    kind: 'addressable' | 'pwm';
    enabled: boolean;
    // Addressable
    r: number;
    g: number;
    b: number;
    brightness: number;
    // PWM
    pwm: number; // 0..4095
  }

  let nodeIp = $state('');
  let config = $state<DeviceConfig | null>(null);
  let loadingConfig = $state(true);
  let toast = $state('');
  let toastTimeout: ReturnType<typeof setTimeout> | null = null;

  // Selected channels (UI selection — may differ from controlled)
  let selectedIndices = $state<number[]>([0]);

  // Desk channel values (16 channels)
  let channels = $state<ChannelDeskState[]>(
    Array.from({ length: 16 }, (_, i) => ({
      index: i,
      name: i < 8 ? `A${i + 1}` : `P${i - 7}`,
      kind: i < 8 ? 'addressable' : 'pwm',
      enabled: true,
      r: 255,
      g: 255,
      b: 255,
      brightness: 255,
      pwm: 0,
    }))
  );

  // ── Manual Session State Machine ─────────────────────────────────────────
  let sessionState = $state<ManualSessionState>('idle');
  let controlledIndices = $state<number[]>([]);
  let sessionRequestedMask = $state(0);
  let leaseRemainingSec = $state(0);
  let sessionErrorMessage = $state<string | null>(null);

  const session = new ManualSession((snap: ManualSessionSnapshot) => {
    sessionState = snap.state;
    controlledIndices = snap.controlledIndices;
    sessionRequestedMask = snap.requestedMask;
    leaseRemainingSec = snap.leaseRemainingSec;
    sessionErrorMessage = snap.errorMessage;
  });

  let isManualActive = $derived(sessionState === 'active');
  let isSessionBusy = $derived(sessionState === 'acquiring' || sessionState === 'releasing');
  let isSessionLostOrError = $derived(sessionState === 'lost' || sessionState === 'error');

  // Whether UI selection differs from controlled set (for Update Targets visibility)
  let selectionDiffersFromControlled = $derived.by(() => {
    if (!isManualActive) return false;
    const selSet = new Set(selectedIndices);
    const ctrlSet = new Set(controlledIndices);
    if (selSet.size !== ctrlSet.size) return true;
    for (const idx of selSet) {
      if (!ctrlSet.has(idx)) return true;
    }
    return false;
  });

  // Controlled channel names for ownership summary badge
  let controlledLabels = $derived(
    controlledIndices.map(i => i < 8 ? `A${i + 1}` : `P${i - 7}`).join(' '),
  );

  let sendThrottleTimer: ReturnType<typeof setTimeout> | null = null;
  let lastSendTimestamp = 0;

  // Save Scene Dialog
  let saveDialogOpen = $state(false);
  let sceneNameInput = $state('Live Look');
  let savingScene = $state(false);
  let saveSceneError = $state('');

  // Live telemetry from nodeStore
  let liveStatus = $derived($nodeStore.status);
  let liveHealth = $derived($nodeStore.health);
  let isConnected = $derived($nodeStore.connection === 'connected');
  let dominantOwner = $derived(liveStatus?.controlOwner ?? 'Idle');

  // Per-channel owners from telemetry
  let channelOwners = $derived.by(() => {
    const map = new Map<number, string>();
    if (liveStatus?.channels) {
      for (const ch of liveStatus.channels) {
        map.set(ch.index, ch.owner);
      }
    }
    return map;
  });

  // Watch telemetry for manual status — delegate to session (server-authoritative)
  $effect(() => {
    if (liveStatus?.manual) {
      session.onTelemetry(liveStatus.manual);
    }
  });

  // Derived selections
  let selectedA = $derived(selectedIndices.filter(i => i < 8));
  let selectedP = $derived(selectedIndices.filter(i => i >= 8));

  // Current common or first values for controls
  let activeAChannel = $derived(selectedA.length > 0 ? channels[selectedA[0]] : null);
  let activePChannel = $derived(selectedP.length > 0 ? channels[selectedP[0]] : null);
  let activePPct = $derived(activePChannel ? Math.round((activePChannel.pwm / 4095) * 100) : 0);

  // Hex color for color picker
  let currentColorHex = $derived.by(() => {
    if (!activeAChannel) return '#ffffff';
    const toHex = (n: number) => n.toString(16).padStart(2, '0');
    return `#${toHex(activeAChannel.r)}${toHex(activeAChannel.g)}${toHex(activeAChannel.b)}`;
  });

  // Per-channel ownership detail: manual armed vs output owner
  let channelOwnershipDetail = $derived.by(() => {
    const details = new Map<number, { armed: boolean; outputOwner: string }>();
    for (const idx of selectedIndices) {
      const owner = channelOwners.get(idx) ?? 'Idle';
      const armed = controlledIndices.includes(idx);
      details.set(idx, { armed, outputOwner: owner });
    }
    return details;
  });

  // Preemption warnings for selected channels
  let preemptionWarning = $derived.by(() => {
    const dmxChannels: string[] = [];
    let hasFactory = false;
    let hasEStop = false;

    for (const idx of selectedIndices) {
      const owner = channelOwners.get(idx);
      if (owner === 'LiveDmx' || owner === 'DMX') {
        dmxChannels.push(idx < 8 ? `A${idx + 1}` : `P${idx - 7}`);
      } else if (owner === 'Factory') {
        hasFactory = true;
      } else if (owner === 'EStop') {
        hasEStop = true;
      }
    }

    if (hasEStop) {
      return { tone: 'danger', message: 'Emergency Stop is active. All outputs are halted.' };
    }
    if (hasFactory) {
      return { tone: 'warning', message: 'Factory test currently has priority on selected channels.' };
    }
    if (dmxChannels.length > 0) {
      return {
        tone: 'info',
        message: `DMX currently has priority on ${dmxChannels.join(', ')}. Manual values are retained and will resume if DMX releases before the manual lease expires.`,
      };
    }
    return null;
  });

  function showToast(msg: string) {
    toast = msg;
    if (toastTimeout) clearTimeout(toastTimeout);
    toastTimeout = setTimeout(() => {
      toast = '';
    }, 4000);
  }

  async function loadConfig(ip: string) {
    if (!ip) {
      loadingConfig = false;
      return;
    }
    loadingConfig = true;
    try {
      config = await getDeviceConfig(ip);
      if (config?.channels) {
        for (let i = 0; i < 16 && i < config.channels.length; i++) {
          const cfg = config.channels[i];
          channels[i].enabled = cfg.enabled;
          if (cfg.name && cfg.name.trim()) {
            channels[i].name = cfg.name.trim();
          }
        }
      }
    } catch {
      // Configuration fallback
    } finally {
      loadingConfig = false;
    }
  }

  // ── Snapshot Dispatch ──────────────────────────────────────────────────────────

  /**
   * Build a scoped snapshot for only the controlled channels (not all enabled).
   * Used for throttled live-edit sends while session is active.
   */
  function buildControlledSnapshot(): ManualChannelValue[] {
    return buildManualSnapshot(channels as ManualDeskChannel[], controlledIndices);
  }

  async function sendManualSnapshot() {
    if (!nodeIp || !isManualActive) return;
    const now = Date.now();
    lastSendTimestamp = now;

    try {
      const payload = buildControlledSnapshot();
      if (payload.length === 0) return;
      await putManualState(nodeIp, payload, 15_000);
      // Lease countdown is updated from telemetry, not locally
    } catch (e) {
      // Heartbeat handles fail-closed; throttled edits log but don't kill session
      console.warn('Manual state dispatch failed', e);
    }
  }

  /**
   * Throttles live updates to 10 Hz (100 ms).
   */
  function scheduleThrottledSend() {
    if (!isManualActive) return;
    const now = Date.now();
    const elapsed = now - lastSendTimestamp;

    if (elapsed >= 100) {
      if (sendThrottleTimer) {
        clearTimeout(sendThrottleTimer);
        sendThrottleTimer = null;
      }
      sendManualSnapshot();
    } else if (!sendThrottleTimer) {
      sendThrottleTimer = setTimeout(() => {
        sendThrottleTimer = null;
        sendManualSnapshot();
      }, 100 - elapsed);
    }
  }

  function commitImmediateSend() {
    if (!isManualActive) return;
    if (sendThrottleTimer) {
      clearTimeout(sendThrottleTimer);
      sendThrottleTimer = null;
    }
    sendManualSnapshot();
  }

  // ── Lifecycle Controls ────────────────────────────────────────────────────────

  async function takeManualControl() {
    if (!nodeIp) return;
    const ok = await session.acquire(nodeIp, channels as ManualDeskChannel[], selectedIndices);
    if (ok) {
      showToast(`Manual control active · ${controlledLabels}`);
    } else if (sessionErrorMessage) {
      showToast(sessionErrorMessage);
    }
  }

  async function releaseControl() {
    if (!nodeIp) return;
    await session.release(nodeIp);
    showToast('Manual control released to Scene');
  }

  async function updateTargets() {
    if (!nodeIp) return;
    const ok = await session.updateTargets(nodeIp, channels as ManualDeskChannel[], selectedIndices);
    if (ok) {
      showToast(`Targets updated · ${controlledLabels}`);
    } else if (sessionErrorMessage) {
      showToast(sessionErrorMessage);
    }
  }

  function blackoutSelected() {
    for (const idx of selectedIndices) {
      const ch = channels[idx];
      if (ch.kind === 'addressable') {
        ch.r = 0;
        ch.g = 0;
        ch.b = 0;
        ch.brightness = 0;
      } else {
        ch.pwm = 0;
      }
    }
    commitImmediateSend();
    showToast('Selected channels blacked out');
  }

  // ── Target Selection ──────────────────────────────────────────────────────────

  function toggleChannel(index: number, multi = false) {
    if (!channels[index].enabled) return;
    if (!multi) {
      selectedIndices = [index];
      return;
    }
    if (selectedIndices.includes(index)) {
      if (selectedIndices.length > 1) {
        selectedIndices = selectedIndices.filter(i => i !== index);
      }
    } else {
      selectedIndices = [...selectedIndices, index];
    }
  }

  function selectAllAddressable() {
    const list = channels.filter(c => c.index < 8 && c.enabled).map(c => c.index);
    if (list.length > 0) selectedIndices = list;
  }

  function selectAllPwm() {
    const list = channels.filter(c => c.index >= 8 && c.enabled).map(c => c.index);
    if (list.length > 0) selectedIndices = list;
  }

  function selectGroup(group: ChannelGroup) {
    const expanded = expandChannelMask(group.channelMask);
    const valid = expanded.filter(i => i >= 0 && i < 16 && channels[i].enabled);
    if (valid.length > 0) {
      selectedIndices = valid;
    }
  }

  // ── Control Handlers ──────────────────────────────────────────────────────────

  function handleColorInput(hex: string) {
    const r = parseInt(hex.slice(1, 3), 16) || 0;
    const g = parseInt(hex.slice(3, 5), 16) || 0;
    const b = parseInt(hex.slice(5, 7), 16) || 0;

    for (const idx of selectedA) {
      channels[idx].r = r;
      channels[idx].g = g;
      channels[idx].b = b;
    }
    scheduleThrottledSend();
  }

  function handleRgbField(comp: 'r' | 'g' | 'b', valStr: string) {
    const val = Math.max(0, Math.min(255, parseInt(valStr, 10) || 0));
    for (const idx of selectedA) {
      channels[idx][comp] = val;
    }
    scheduleThrottledSend();
  }

  function handleBrightness(val: number) {
    const bri = Math.max(0, Math.min(255, Math.round(val)));
    for (const idx of selectedA) {
      channels[idx].brightness = bri;
    }
    scheduleThrottledSend();
  }

  function handleQuickColor(r: number, g: number, b: number) {
    for (const idx of selectedA) {
      channels[idx].r = r;
      channels[idx].g = g;
      channels[idx].b = b;
    }
    commitImmediateSend();
  }

  function handlePwmFader(pct: number) {
    const clampedPct = Math.max(0, Math.min(100, pct));
    const raw12Bit = Math.round((clampedPct / 100) * 4095);
    for (const idx of selectedP) {
      channels[idx].pwm = raw12Bit;
    }
    scheduleThrottledSend();
  }

  function handleQuickPwm(pct: number) {
    handlePwmFader(pct);
    commitImmediateSend();
  }

  // ── Capture as Scene ──────────────────────────────────────────────────────────

  function openSaveSceneDialog() {
    saveSceneError = '';
    const nowTime = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    sceneNameInput = `Live Look ${nowTime}`;
    saveDialogOpen = true;
  }

  async function handleConfirmSaveScene(name: string) {
    if (!nodeIp) return;
    savingScene = true;
    saveSceneError = '';

    try {
      const presets = await getPresets(nodeIp);
      // Find lowest free slot index 0..31
      const usedIndices = new Set(presets.map(p => p.index));
      let freeSlot = -1;
      for (let i = 0; i < MAX_PRESETS; i++) {
        if (!usedIndices.has(i)) {
          freeSlot = i;
          break;
        }
      }

      if (freeSlot === -1) {
        saveSceneError = `All ${MAX_PRESETS} scene preset slots are occupied. Manage scenes in Scene Studio to free a slot.`;
        savingScene = false;
        return;
      }

      const activeManuals = buildControlledSnapshot();
      const newPreset = captureLiveDeskToScene(freeSlot, name.trim() || `Live Look ${freeSlot}`, activeManuals);

      await savePreset(nodeIp, newPreset);
      saveDialogOpen = false;
      showToast(`Saved as Scene ${freeSlot}: "${newPreset.name}"`);
    } catch (e) {
      saveSceneError = 'Failed to save scene: ' + (e instanceof Error ? e.message : String(e));
    } finally {
      savingScene = false;
    }
  }

  // ── Lifecycle Hooks ───────────────────────────────────────────────────────────

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    if (nodeIp) {
      loadConfig(nodeIp);
    }
  });

  onDestroy(() => {
    if (sendThrottleTimer) clearTimeout(sendThrottleTimer);
    session.destroy(nodeIp);
  });

  // Color Swatches
  const QUICK_COLORS = [
    { label: 'White', r: 255, g: 255, b: 255, bg: '#ffffff' },
    { label: 'Warm', r: 255, g: 180, b: 100, bg: '#ffb464' },
    { label: 'Red', r: 255, g: 0, b: 0, bg: '#ef4444' },
    { label: 'Amber', r: 245, g: 158, b: 11, bg: '#f59e0b' },
    { label: 'Yellow', r: 234, g: 179, b: 8, bg: '#eab308' },
    { label: 'Green', r: 34, g: 197, b: 94, bg: '#22c55e' },
    { label: 'Cyan', r: 6, g: 182, b: 212, bg: '#06b6d4' },
    { label: 'Magenta', r: 217, g: 70, b: 239, bg: '#d946ef' },
  ];
</script>

<div class="live-root">
  <!-- Toast message -->
  {#if toast}
    <div class="live-toast-bar">
      <span>{toast}</span>
    </div>
  {/if}

  <!-- Header Section -->
  <header class="live-header">
    <div class="live-header-main">
      <div class="live-title-row">
        <h1 class="live-title">Live Desk</h1>
        <span class="live-connection-badge" class:connected={isConnected}>
          <span class="live-status-dot"></span>
          {isConnected ? (nodeIp || 'Connected') : 'Disconnected'}
        </span>
      </div>
      <p class="live-subtitle">Transient channel-scoped lighting desk & live arbitration control</p>
    </div>

    <!-- Status Cards Bar -->
    <div class="live-telemetry-row">
      <div class="live-telemetry-item">
        <span class="live-telemetry-label">Arbitration Owner</span>
        <span class="live-telemetry-val" class:manual-source={dominantOwner === 'ManualOverride'}>
          {dominantOwner}
        </span>
      </div>

      <div class="live-telemetry-item">
        <span class="live-telemetry-label">Manual Session</span>
        <span class="live-telemetry-val" class:live-active={isManualActive} class:live-lost={isSessionLostOrError}>
          {#if sessionState === 'active'}
            Active ({leaseRemainingSec}s)
          {:else if sessionState === 'acquiring'}
            Acquiring…
          {:else if sessionState === 'releasing'}
            Releasing…
          {:else if sessionState === 'lost'}
            Lost
          {:else if sessionState === 'error'}
            Error
          {:else}
            Standby
          {/if}
        </span>
      </div>

      {#if isManualActive && controlledIndices.length > 0}
        <div class="live-telemetry-item">
          <span class="live-telemetry-label">Manual Lease</span>
          <span class="live-telemetry-val live-active">
            {controlledLabels} · {leaseRemainingSec}s
          </span>
        </div>
      {/if}

      {#if liveHealth?.output?.current_power_mw != null || liveStatus?.powerMw != null}
        <div class="live-telemetry-item">
          <span class="live-telemetry-label">Power Draw</span>
          <span class="live-telemetry-val">
            {(((liveHealth?.output?.current_power_mw ?? liveStatus?.powerMw ?? 0)) / 1000).toFixed(1)} W
          </span>
        </div>
      {/if}

      {#if liveHealth?.output?.fault}
        <div class="live-telemetry-item live-fault-item">
          <span class="live-telemetry-label">Output Fault</span>
          <span class="live-telemetry-val">FAULT DETECTED</span>
        </div>
      {/if}
    </div>
  </header>

  <!-- Preemption Alert -->
  {#if preemptionWarning}
    <div class="live-preemption-alert" class:danger={preemptionWarning.tone === 'danger'} class:warning={preemptionWarning.tone === 'warning'}>
      <span class="live-alert-icon">⚠️</span>
      <p class="live-alert-text">{preemptionWarning.message}</p>
    </div>
  {/if}

  <!-- Session Lost/Error Banner -->
  {#if isSessionLostOrError && sessionErrorMessage}
    <div class="live-preemption-alert danger">
      <span class="live-alert-icon">⚡</span>
      <p class="live-alert-text">{sessionErrorMessage}</p>
    </div>
  {/if}

  <!-- Actions Bar -->
  <div class="live-actions-bar">
    <div class="live-actions-left">
      {#if sessionState === 'idle' || sessionState === 'lost' || sessionState === 'error'}
        <button
          type="button"
          class="live-action-btn live-btn-take"
          onclick={takeManualControl}
          disabled={!isConnected || isSessionBusy}
        >
          {sessionState === 'lost' || sessionState === 'error' ? 'Re-acquire Control' : 'Take Manual Control'}
        </button>
      {:else if sessionState === 'active'}
        <button
          type="button"
          class="live-action-btn live-btn-release"
          onclick={releaseControl}
        >
          Release to Scene
        </button>

        {#if selectionDiffersFromControlled}
          <button
            type="button"
            class="live-action-btn live-btn-update-targets"
            onclick={updateTargets}
          >
            Update Targets
          </button>
        {/if}
      {:else}
        <button
          type="button"
          class="live-action-btn live-btn-take"
          disabled
        >
          {sessionState === 'acquiring' ? 'Acquiring…' : 'Releasing…'}
        </button>
      {/if}

      <button
        type="button"
        class="live-action-btn live-btn-blackout"
        onclick={blackoutSelected}
        disabled={selectedIndices.length === 0}
      >
        Blackout Selected
      </button>
    </div>

    <div class="live-actions-right">
      <button
        type="button"
        class="live-action-btn live-btn-capture"
        onclick={openSaveSceneDialog}
        disabled={!isConnected}
      >
        Save as Scene
      </button>
    </div>
  </div>

  <!-- Target Selection Workspace -->
  <section class="live-workspace-section" aria-label="Channel Selection">
    <div class="live-workspace-header">
      <h2 class="live-section-title">TARGET SELECTION</h2>
      <span class="live-selection-count">
        {selectedIndices.length} channel{selectedIndices.length === 1 ? '' : 's'} selected
      </span>
    </div>

    <!-- Groups Row -->
    <div class="live-groups-bar">
      <span class="live-groups-label">GROUPS:</span>
      <button
        type="button"
        class="live-group-chip"
        onclick={selectAllAddressable}
      >
        All Addressable (A1–A8)
      </button>
      <button
        type="button"
        class="live-group-chip"
        onclick={selectAllPwm}
      >
        All PWM (P1–P8)
      </button>

      {#if config?.groups}
        {#each config.groups as grp, gIdx}
          {#if grp.enabled && grp.channelMask > 0}
            <button
              type="button"
              class="live-group-chip"
              onclick={() => selectGroup(grp)}
            >
              {grp.name || `Group ${gIdx + 1}`}
            </button>
          {/if}
        {/each}
      {/if}
    </div>

    <!-- Physical Channel Buttons Grid -->
    <div class="live-channels-matrix">
      <!-- Addressable Ports A1..A8 -->
      <div class="live-ports-block live-ports-addressable">
        <span class="live-ports-label">ADDRESSABLE (A1–A8)</span>
        <div class="live-ports-row">
          {#each channels.slice(0, 8) as ch}
            {@const isSel = selectedIndices.includes(ch.index)}
            {@const isCtrl = controlledIndices.includes(ch.index)}
            {@const owner = channelOwners.get(ch.index)}
            <button
              type="button"
              class="live-port-btn live-port-a"
              class:selected={isSel}
              class:controlled={isCtrl}
              class:disabled={!ch.enabled}
              disabled={!ch.enabled}
              onclick={(e) => toggleChannel(ch.index, e.shiftKey || e.ctrlKey || e.metaKey)}
            >
              <span class="live-port-id">A{ch.index + 1}</span>
              <span class="live-port-name">{ch.name !== `A${ch.index + 1}` ? ch.name : ''}</span>
              {#if isCtrl && owner && owner !== 'ManualOverride'}
                <span class="live-port-owner live-port-owner-armed">Manual armed · {owner} owns</span>
              {:else if owner}
                <span class="live-port-owner">{owner}</span>
              {/if}
            </button>
          {/each}
        </div>
      </div>

      <!-- PWM Ports P1..P8 -->
      <div class="live-ports-block live-ports-pwm">
        <span class="live-ports-label">PWM (P1–P8)</span>
        <div class="live-ports-row">
          {#each channels.slice(8, 16) as ch}
            {@const isSel = selectedIndices.includes(ch.index)}
            {@const isCtrl = controlledIndices.includes(ch.index)}
            {@const owner = channelOwners.get(ch.index)}
            <button
              type="button"
              class="live-port-btn live-port-p"
              class:selected={isSel}
              class:controlled={isCtrl}
              class:disabled={!ch.enabled}
              disabled={!ch.enabled}
              onclick={(e) => toggleChannel(ch.index, e.shiftKey || e.ctrlKey || e.metaKey)}
            >
              <span class="live-port-id">P{ch.index - 7}</span>
              <span class="live-port-name">{ch.name !== `P${ch.index - 7}` ? ch.name : ''}</span>
              {#if isCtrl && owner && owner !== 'ManualOverride'}
                <span class="live-port-owner live-port-owner-armed">Manual armed · {owner} owns</span>
              {:else if owner}
                <span class="live-port-owner">{owner}</span>
              {/if}
            </button>
          {/each}
        </div>
      </div>
    </div>
  </section>

  <!-- Controls Workspaces -->
  <div class="live-desk-panels">
    <!-- Addressable Controls Panel -->
    {#if selectedA.length > 0 && activeAChannel}
      <section class="live-panel-box live-panel-addressable" aria-label="Addressable Color Controls">
        <div class="live-panel-header">
          <div class="live-panel-title-group">
            <span class="live-tag live-tag-magenta">ADDRESSABLE</span>
            <h2 class="live-panel-heading">Color & Intensity</h2>
          </div>
          <span class="live-panel-scope">
            Targeting: {selectedA.map(i => `A${i + 1}`).join(', ')}
          </span>
        </div>

        <div class="live-controls-flow">
          <!-- Color Picker & Swatch Preview -->
          <div class="live-color-master-row">
            <div
              class="live-swatch-display"
              style="background-color: rgb({activeAChannel.r}, {activeAChannel.g}, {activeAChannel.b}); opacity: {activeAChannel.brightness / 255};"
            ></div>

            <div class="live-color-input-wrap">
              <label for="live-color-hex" class="live-field-label">Color Picker</label>
              <input
                id="live-color-hex"
                type="color"
                class="live-color-native-input"
                value={currentColorHex}
                oninput={(e) => handleColorInput((e.target as HTMLInputElement).value)}
                onchange={() => commitImmediateSend()}
              />
            </div>

            <!-- Numeric RGB Fields -->
            <div class="live-rgb-inputs-wrap">
              <div class="live-rgb-field">
                <label for="live-rgb-r" class="live-rgb-label">R</label>
                <input
                  id="live-rgb-r"
                  type="number"
                  min="0"
                  max="255"
                  class="live-num-input"
                  value={activeAChannel.r}
                  oninput={(e) => handleRgbField('r', (e.target as HTMLInputElement).value)}
                  onchange={() => commitImmediateSend()}
                />
              </div>

              <div class="live-rgb-field">
                <label for="live-rgb-g" class="live-rgb-label">G</label>
                <input
                  id="live-rgb-g"
                  type="number"
                  min="0"
                  max="255"
                  class="live-num-input"
                  value={activeAChannel.g}
                  oninput={(e) => handleRgbField('g', (e.target as HTMLInputElement).value)}
                  onchange={() => commitImmediateSend()}
                />
              </div>

              <div class="live-rgb-field">
                <label for="live-rgb-b" class="live-rgb-label">B</label>
                <input
                  id="live-rgb-b"
                  type="number"
                  min="0"
                  max="255"
                  class="live-num-input"
                  value={activeAChannel.b}
                  oninput={(e) => handleRgbField('b', (e.target as HTMLInputElement).value)}
                  onchange={() => commitImmediateSend()}
                />
              </div>
            </div>
          </div>

          <!-- Quick Palette -->
          <div class="live-palette-row">
            {#each QUICK_COLORS as qc}
              <button
                type="button"
                class="live-palette-swatch"
                style="background-color: {qc.bg};"
                title={qc.label}
                onclick={() => handleQuickColor(qc.r, qc.g, qc.b)}
              >
                <span class="live-sr-only">{qc.label}</span>
              </button>
            {/each}
          </div>

          <!-- Brightness Slider -->
          <div class="live-slider-block">
            <div class="live-slider-header">
              <label for="live-bri-slider" class="live-field-label">Brightness</label>
              <span class="live-slider-val">{activeAChannel.brightness} / 255</span>
            </div>
            <input
              id="live-bri-slider"
              type="range"
              min="0"
              max="255"
              class="live-range-fader"
              value={activeAChannel.brightness}
              oninput={(e) => handleBrightness(Number((e.target as HTMLInputElement).value))}
              onchange={() => commitImmediateSend()}
            />
          </div>
        </div>
      </section>
    {/if}

    <!-- PWM Controls Panel -->
    {#if selectedP.length > 0 && activePChannel}
      <section class="live-panel-box live-panel-pwm" aria-label="PWM Fader Controls">
        <div class="live-panel-header">
          <div class="live-panel-title-group">
            <span class="live-tag live-tag-amber">PWM</span>
            <h2 class="live-panel-heading">Duty Level & Dimmer</h2>
          </div>
          <span class="live-panel-scope">
            Targeting: {selectedP.map(i => `P${i - 7}`).join(', ')}
          </span>
        </div>

        <div class="live-controls-flow">
          <!-- Fader Level Display -->
          <div class="live-pwm-display-row">
            <div class="live-pwm-metric">
              <span class="live-pwm-pct-huge">{activePPct}%</span>
              <span class="live-pwm-subtext">12-bit Raw: {activePChannel.pwm} / 4095</span>
            </div>

            <!-- Quick Preset Steps -->
            <div class="live-pwm-presets">
              {#each [0, 25, 50, 75, 100] as step}
                <button
                  type="button"
                  class="live-step-btn"
                  class:active={activePPct === step}
                  onclick={() => handleQuickPwm(step)}
                >
                  {step}%
                </button>
              {/each}
            </div>
          </div>

          <!-- Fader Slider -->
          <div class="live-slider-block">
            <div class="live-slider-header">
              <label for="live-pwm-slider" class="live-field-label">PWM Master Fader</label>
              <span class="live-slider-val">{activePPct}%</span>
            </div>
            <input
              id="live-pwm-slider"
              type="range"
              min="0"
              max="100"
              class="live-range-fader live-range-pwm"
              value={activePPct}
              oninput={(e) => handlePwmFader(Number((e.target as HTMLInputElement).value))}
              onchange={() => commitImmediateSend()}
            />
          </div>
        </div>
      </section>
    {/if}

    <!-- Empty Selection Message -->
    {#if selectedIndices.length === 0}
      <div class="live-empty-selection">
        <p class="live-empty-text">Select at least one physical channel or group above to take manual control.</p>
      </div>
    {/if}
  </div>

  <!-- Save as Scene Dialog -->
  <OperatorDialog
    open={saveDialogOpen}
    title="Save Live Desk as Scene"
    description={saveSceneError || "Capture active Live Desk manual values into a new stored Scene preset."}
    tone={saveSceneError ? 'danger' : 'neutral'}
    inputLabel="Scene Name"
    inputPlaceholder="e.g. Primetime Accent"
    bind:value={sceneNameInput}
    busy={savingScene}
    confirmLabel="Capture & Save"
    onconfirm={() => handleConfirmSaveScene(sceneNameInput)}
    oncancel={() => { saveDialogOpen = false; saveSceneError = ''; }}
  />
</div>

<style>
  /* Base Layout */
  .live-root {
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
    padding: 1.5rem;
    max-width: 1200px;
    margin: 0 auto;
    color: #f4f4f5;
  }

  /* Toast */
  .live-toast-bar {
    position: fixed;
    bottom: 2rem;
    right: 2rem;
    background: #18181b;
    border: 1px solid #d946ef;
    color: #f4f4f5;
    padding: 0.75rem 1.25rem;
    border-radius: 6px;
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.5);
    z-index: 50;
    font-size: 0.875rem;
    animation: live-toast-in 0.2s ease-out;
  }

  @keyframes live-toast-in {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
  }

  /* Header */
  .live-header {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    border-bottom: 1px solid #27272a;
    padding-bottom: 1.25rem;
  }

  .live-title-row {
    display: flex;
    align-items: center;
    gap: 1rem;
  }

  .live-title {
    font-size: 1.75rem;
    font-weight: 700;
    letter-spacing: -0.025em;
    margin: 0;
    color: #fafafa;
  }

  .live-subtitle {
    font-size: 0.875rem;
    color: #a1a1aa;
    margin: 0.25rem 0 0;
  }

  .live-connection-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.75rem;
    padding: 0.25rem 0.6rem;
    border-radius: 9999px;
    background: #27272a;
    color: #a1a1aa;
  }

  .live-connection-badge.connected {
    background: rgba(34, 197, 94, 0.15);
    color: #4ade80;
    border: 1px solid rgba(34, 197, 94, 0.3);
  }

  .live-status-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: currentColor;
  }

  /* Telemetry row */
  .live-telemetry-row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.75rem;
  }

  .live-telemetry-item {
    background: #18181b;
    border: 1px solid #27272a;
    border-radius: 6px;
    padding: 0.5rem 0.875rem;
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    min-width: 140px;
  }

  .live-telemetry-label {
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #71717a;
  }

  .live-telemetry-val {
    font-size: 0.95rem;
    font-weight: 600;
    color: #e4e4e7;
    font-family: ui-monospace, monospace;
  }

  .live-telemetry-val.live-active {
    color: #d946ef;
  }

  .live-telemetry-val.live-lost {
    color: #f87171;
  }

  .live-telemetry-val.manual-source {
    color: #f59e0b;
  }

  .live-fault-item {
    border-color: #ef4444;
    background: rgba(239, 68, 68, 0.1);
  }

  .live-fault-item .live-telemetry-val {
    color: #f87171;
  }

  /* Alerts */
  .live-preemption-alert {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.75rem 1rem;
    border-radius: 6px;
    background: #18181b;
    border: 1px solid #27272a;
    font-size: 0.875rem;
  }

  .live-preemption-alert.warning {
    border-color: #f59e0b;
    background: rgba(245, 158, 11, 0.08);
    color: #fbbf24;
  }

  .live-preemption-alert.danger {
    border-color: #ef4444;
    background: rgba(239, 68, 68, 0.1);
    color: #fca5a5;
  }

  .live-alert-icon {
    font-size: 1.1rem;
  }

  .live-alert-text {
    margin: 0;
  }

  /* Actions Bar */
  .live-actions-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.75rem;
  }

  .live-actions-left,
  .live-actions-right {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    flex-wrap: wrap;
  }

  .live-action-btn {
    padding: 0.6rem 1.1rem;
    font-size: 0.875rem;
    font-weight: 600;
    border-radius: 6px;
    border: 1px solid transparent;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .live-action-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .live-btn-take {
    background: #d946ef;
    color: #ffffff;
  }

  .live-btn-take:hover:not(:disabled) {
    background: #c026d3;
  }

  .live-btn-release {
    background: #27272a;
    border-color: #3f3f46;
    color: #f4f4f5;
  }

  .live-btn-release:hover:not(:disabled) {
    background: #3f3f46;
  }

  .live-btn-update-targets {
    background: rgba(217, 70, 239, 0.15);
    border: 1px solid #d946ef;
    color: #f0abfc;
  }

  .live-btn-update-targets:hover:not(:disabled) {
    background: rgba(217, 70, 239, 0.25);
  }

  .live-btn-blackout {
    background: #18181b;
    border-color: #3f3f46;
    color: #a1a1aa;
  }

  .live-btn-blackout:hover:not(:disabled) {
    color: #ffffff;
    border-color: #52525b;
  }

  .live-btn-capture {
    background: #18181b;
    border: 1px solid #d946ef;
    color: #d946ef;
  }

  .live-btn-capture:hover:not(:disabled) {
    background: rgba(217, 70, 239, 0.15);
  }

  /* Target Selection Section */
  .live-workspace-section {
    background: #18181b;
    border: 1px solid #27272a;
    border-radius: 8px;
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  .live-workspace-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .live-section-title {
    font-size: 0.8rem;
    font-weight: 700;
    letter-spacing: 0.05em;
    color: #71717a;
    margin: 0;
  }

  .live-selection-count {
    font-size: 0.8rem;
    color: #a1a1aa;
  }

  /* Groups */
  .live-groups-bar {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    flex-wrap: wrap;
    border-bottom: 1px solid #27272a;
    padding-bottom: 0.75rem;
  }

  .live-groups-label {
    font-size: 0.75rem;
    font-weight: 600;
    color: #71717a;
  }

  .live-group-chip {
    padding: 0.35rem 0.75rem;
    font-size: 0.75rem;
    font-weight: 500;
    background: #27272a;
    border: 1px solid #3f3f46;
    color: #d4d4d8;
    border-radius: 4px;
    cursor: pointer;
    transition: all 0.1s ease;
  }

  .live-group-chip:hover {
    background: #3f3f46;
    color: #ffffff;
  }

  /* Channels Matrix */
  .live-channels-matrix {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  .live-ports-block {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .live-ports-label {
    font-size: 0.7rem;
    font-weight: 600;
    color: #a1a1aa;
    text-transform: uppercase;
  }

  .live-ports-row {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(80px, 1fr));
    gap: 0.5rem;
  }

  .live-port-btn {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 0.6rem 0.4rem;
    border-radius: 6px;
    background: #09090b;
    border: 1px solid #27272a;
    cursor: pointer;
    transition: all 0.15s ease;
    min-height: 52px;
  }

  .live-port-btn.disabled {
    opacity: 0.3;
    cursor: not-allowed;
  }

  .live-port-id {
    font-size: 0.95rem;
    font-weight: 700;
    font-family: ui-monospace, monospace;
    color: #f4f4f5;
  }

  .live-port-name {
    font-size: 0.65rem;
    color: #71717a;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 70px;
  }

  .live-port-owner {
    font-size: 0.6rem;
    color: #06b6d4;
    text-transform: uppercase;
    font-family: ui-monospace, monospace;
  }

  .live-port-owner-armed {
    color: #f59e0b;
  }

  .live-port-btn.controlled:not(.selected) {
    border-color: rgba(217, 70, 239, 0.45);
    background: rgba(217, 70, 239, 0.06);
  }

  /* Port selection styles */
  .live-port-a:hover:not(.disabled) {
    border-color: rgba(217, 70, 239, 0.4);
  }

  .live-port-a.selected {
    border-color: #d946ef;
    background: rgba(217, 70, 239, 0.15);
    box-shadow: 0 0 10px rgba(217, 70, 239, 0.2);
  }

  .live-port-p:hover:not(.disabled) {
    border-color: rgba(245, 158, 11, 0.4);
  }

  .live-port-p.selected {
    border-color: #f59e0b;
    background: rgba(245, 158, 11, 0.15);
    box-shadow: 0 0 10px rgba(245, 158, 11, 0.2);
  }

  /* Control Panels */
  .live-desk-panels {
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
  }

  .live-panel-box {
    background: #18181b;
    border: 1px solid #27272a;
    border-radius: 8px;
    padding: 1.5rem;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }

  .live-panel-addressable {
    border-top: 3px solid #d946ef;
  }

  .live-panel-pwm {
    border-top: 3px solid #f59e0b;
  }

  .live-panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid #27272a;
    padding-bottom: 0.75rem;
  }

  .live-panel-title-group {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }

  .live-tag {
    font-size: 0.7rem;
    font-weight: 700;
    padding: 0.15rem 0.45rem;
    border-radius: 4px;
    letter-spacing: 0.05em;
  }

  .live-tag-magenta {
    background: rgba(217, 70, 239, 0.2);
    color: #e879f9;
  }

  .live-tag-amber {
    background: rgba(245, 158, 11, 0.2);
    color: #fbbf24;
  }

  .live-panel-heading {
    font-size: 1.1rem;
    font-weight: 600;
    margin: 0;
    color: #f4f4f5;
  }

  .live-panel-scope {
    font-size: 0.8rem;
    color: #a1a1aa;
    font-family: ui-monospace, monospace;
  }

  .live-controls-flow {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }

  /* Color Row */
  .live-color-master-row {
    display: flex;
    align-items: center;
    gap: 1.5rem;
    flex-wrap: wrap;
  }

  .live-swatch-display {
    width: 64px;
    height: 64px;
    border-radius: 8px;
    border: 2px solid #3f3f46;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
    flex-shrink: 0;
  }

  .live-color-input-wrap {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  .live-field-label {
    font-size: 0.75rem;
    font-weight: 600;
    color: #a1a1aa;
  }

  .live-color-native-input {
    width: 48px;
    height: 38px;
    padding: 0;
    border: 1px solid #3f3f46;
    border-radius: 4px;
    background: none;
    cursor: pointer;
  }

  .live-rgb-inputs-wrap {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }

  .live-rgb-field {
    display: flex;
    align-items: center;
    background: #09090b;
    border: 1px solid #3f3f46;
    border-radius: 4px;
    padding: 0.2rem 0.5rem;
    gap: 0.4rem;
  }

  .live-rgb-label {
    font-size: 0.75rem;
    font-weight: 700;
    color: #71717a;
    font-family: ui-monospace, monospace;
  }

  .live-num-input {
    width: 48px;
    background: transparent;
    border: none;
    color: #fafafa;
    font-size: 0.875rem;
    font-family: ui-monospace, monospace;
    text-align: right;
  }

  .live-num-input:focus {
    outline: none;
  }

  /* Palette Swatches */
  .live-palette-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    flex-wrap: wrap;
  }

  .live-palette-swatch {
    width: 32px;
    height: 32px;
    border-radius: 6px;
    border: 1px solid rgba(255, 255, 255, 0.15);
    cursor: pointer;
    transition: transform 0.1s ease;
  }

  .live-palette-swatch:hover {
    transform: scale(1.1);
  }

  /* Sliders */
  .live-slider-block {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .live-slider-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .live-slider-val {
    font-size: 0.85rem;
    font-family: ui-monospace, monospace;
    color: #a1a1aa;
  }

  .live-range-fader {
    width: 100%;
    accent-color: #d946ef;
    cursor: pointer;
    height: 6px;
  }

  .live-range-pwm {
    accent-color: #f59e0b;
  }

  /* PWM Display */
  .live-pwm-display-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 1rem;
  }

  .live-pwm-metric {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
  }

  .live-pwm-pct-huge {
    font-size: 2.25rem;
    font-weight: 800;
    font-family: ui-monospace, monospace;
    color: #fbbf24;
    line-height: 1;
  }

  .live-pwm-subtext {
    font-size: 0.75rem;
    color: #71717a;
    font-family: ui-monospace, monospace;
  }

  .live-pwm-presets {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }

  .live-step-btn {
    padding: 0.4rem 0.75rem;
    font-size: 0.8rem;
    font-weight: 600;
    background: #09090b;
    border: 1px solid #3f3f46;
    color: #d4d4d8;
    border-radius: 4px;
    cursor: pointer;
    transition: all 0.1s ease;
  }

  .live-step-btn:hover {
    background: #27272a;
    color: #ffffff;
  }

  .live-step-btn.active {
    background: rgba(245, 158, 11, 0.2);
    border-color: #f59e0b;
    color: #fbbf24;
  }

  .live-empty-selection {
    padding: 3rem 1.5rem;
    text-align: center;
    background: #18181b;
    border: 1px dashed #27272a;
    border-radius: 8px;
  }

  .live-empty-text {
    color: #71717a;
    font-size: 0.95rem;
    margin: 0;
  }


  .live-sr-only {
    position: absolute;
    width: 1px;
    height: 1px;
    padding: 0;
    margin: -1px;
    overflow: hidden;
    clip: rect(0, 0, 0, 0);
    white-space: nowrap;
    border: 0;
  }
</style>
