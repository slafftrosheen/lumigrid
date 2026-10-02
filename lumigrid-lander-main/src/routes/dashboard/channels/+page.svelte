<script lang="ts">
  import { onMount } from 'svelte';
  import { getChannels, testChannel, type HardwareChannel } from '$lib/api/channels';
  import { getDeviceConfig, saveDeviceConfig, type DeviceConfig, type ChannelConfig, type ChannelGroup } from '$lib/api/config';
  import { putManualState } from '$lib/api/manual';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let channels = $state<HardwareChannel[]>([]);
  let config = $state<DeviceConfig | null>(null);
  let loading = $state(true);
  let error = $state('');
  let toast = $state('');
  let testingChannel = $state<number | null>(null);
  let loadGen = 0;

  // Selected channel for configuration modal
  let selectedChannel = $state<HardwareChannel | null>(null);
  let editForm = $state<{
    enabled: boolean;
    name: string;
    addressableType: 'WS2812' | 'SK6812';
    pixelCount: number;
    pixelOrder: 'RGB' | 'GRB' | 'RGBW' | 'GRBW';
    maxBrightness: number;
    gammaCorrection: boolean;
    powerLimitW: number;
    dmxUniverse: number;
    dmxStartChannel: number;
    dmxMode: number;
    groupBits: boolean[];
    pwmLevel: number; // 0..100%
  }>({
    enabled: false,
    name: '',
    addressableType: 'WS2812',
    pixelCount: 0,
    pixelOrder: 'GRB',
    maxBrightness: 255,
    gammaCorrection: true,
    powerLimitW: 0,
    dmxUniverse: 0,
    dmxStartChannel: 0,
    dmxMode: 0,
    groupBits: [false, false, false, false, false, false, false, false],
    pwmLevel: 0,
  });
  let savingChannel = $state(false);

  // Split channels into the two immutable hardware banks
  const addressableChannels = $derived.by(() => {
    return channels.filter(c => c.index < 8);
  });

  const pwmChannels = $derived.by(() => {
    return channels.filter(c => c.index >= 8 && c.index < 16);
  });

  const addressableActiveCount = $derived.by(() => {
    return addressableChannels.filter(c => c.enabled).length;
  });

  const pwmActiveCount = $derived.by(() => {
    return pwmChannels.filter(c => c.enabled).length;
  });

  let isAdmin = $derived($nodeStore.dashboardRole === 'admin');

  // Groups state
  let editingGroup = $state<ChannelGroup | null>(null);
  let editingGroupIdx = $state<number>(-1);
  let groupForm = $state<{
    name: string;
    enabled: boolean;
    channels: boolean[];
  }>({
    name: '',
    enabled: false,
    channels: Array(16).fill(false),
  });
  let savingGroup = $state(false);

  const displayGroups = $derived.by(() => {
    if (!config?.groups) return [];
    return config.groups.slice(0, 8).map((grp, idx) => {
      const mask = grp.channelMask ?? 0;
      const memberChannels: string[] = [];
      for (let i = 0; i < 16; i++) {
        if ((mask & (1 << i)) !== 0) {
          memberChannels.push(i < 8 ? `A${i + 1}` : `P${i - 7}`);
        }
      }
      return {
        ...grp,
        index: idx,
        name: grp.name || `Group ${idx + 1}`,
        memberChannels,
        memberCount: memberChannels.length,
      };
    });
  });

  const activeGroupCount = $derived.by(() => {
    return displayGroups.filter(g => g.enabled).length;
  });

  // Local tracking of PWM duty for live sliders (0..100%)
  let pwmLevels = $state<Record<number, number>>({
    8: 0, 9: 0, 10: 0, 11: 0, 12: 0, 13: 0, 14: 0, 15: 0
  });
  const pwmLastSentAt: Record<number, number> = {};
  const pwmSendTimers: Record<number, ReturnType<typeof setTimeout> | undefined> = {};
  const PWM_SEND_INTERVAL_MS = 50; // 20 Hz max while dragging


  function toastMsg(msg: string) {
    toast = msg;
    setTimeout(() => { if (toast === msg) toast = ''; }, 2800);
  }

  async function loadData(targetIp = nodeIp, gen = loadGen) {
    if (!targetIp) {
      channels = [];
      config = null;
      loading = false;
      return;
    }
    loading = true;
    error = '';
    try {
      const [chList, cfg] = await Promise.all([
        getChannels(targetIp),
        getDeviceConfig(targetIp).catch(() => null),
      ]);
      if (gen !== loadGen) return;
      channels = chList;
      config = cfg;
    } catch (e) {
      if (gen !== loadGen) return;
      error = String(e);
    } finally {
      if (gen === loadGen) loading = false;
    }
  }

  async function runIdentify(ch: HardwareChannel, e?: MouseEvent) {
    if (e) e.stopPropagation();
    if (!nodeIp || testingChannel !== null) return;
    testingChannel = ch.index;
    try {
      await testChannel(nodeIp, ch.index, 'identify', 3000);
      toastMsg(`Testing ${ch.port}…`);
    } catch (err) {
      toastMsg(`Test failed: ${String(err)}`);
    } finally {
      setTimeout(() => {
        if (testingChannel === ch.index) testingChannel = null;
      }, 3000);
    }
  }

  async function sendPwmLevel(ch: HardwareChannel, percent: number) {
    if (!nodeIp) return;
    const clamped = Math.max(0, Math.min(100, percent));
    const duty12 = Math.round((clamped / 100) * 4095);
    pwmLastSentAt[ch.index] = Date.now();
    try {
      await putManualState(nodeIp, [{ index: ch.index, pwm: duty12 }], 15_000);
    } catch (err) {
      console.error('Failed to set PWM duty:', err);
    }
  }

  function handlePwmSliderInput(ch: HardwareChannel, percent: number) {
    pwmLevels[ch.index] = percent;
    if (!nodeIp) return;

    const elapsed = Date.now() - (pwmLastSentAt[ch.index] ?? 0);
    if (elapsed >= PWM_SEND_INTERVAL_MS) {
      if (pwmSendTimers[ch.index]) {
        clearTimeout(pwmSendTimers[ch.index]);
        pwmSendTimers[ch.index] = undefined;
      }
      void sendPwmLevel(ch, percent);
      return;
    }

    if (pwmSendTimers[ch.index]) clearTimeout(pwmSendTimers[ch.index]);
    pwmSendTimers[ch.index] = setTimeout(() => {
      pwmSendTimers[ch.index] = undefined;
      void sendPwmLevel(ch, pwmLevels[ch.index] ?? percent);
    }, PWM_SEND_INTERVAL_MS - elapsed);
  }

  function handlePwmSliderCommit(ch: HardwareChannel, percent: number) {
    pwmLevels[ch.index] = percent;
    if (pwmSendTimers[ch.index]) {
      clearTimeout(pwmSendTimers[ch.index]);
      pwmSendTimers[ch.index] = undefined;
    }
    // Always send the final released value, even when the drag was throttled.
    void sendPwmLevel(ch, percent);
  }

  function openConfigModal(ch: HardwareChannel) {
    selectedChannel = ch;
    const gBits = [false, false, false, false, false, false, false, false];
    if (config?.groups) {
      for (let g = 0; g < 8; g++) {
        const mask = config.groups[g]?.channelMask ?? 0;
        if ((mask & (1 << ch.index)) !== 0) {
          gBits[g] = true;
        }
      }
    }

    editForm = {
      enabled: ch.enabled,
      name: ch.name || '',
      addressableType: ch.addressable?.type ?? 'WS2812',
      pixelCount: ch.addressable?.pixels ?? 0,
      pixelOrder: ch.addressable?.pixelOrder ?? 'GRB',
      maxBrightness: ch.maxBrightness ?? 255,
      gammaCorrection: ch.gammaCorrection ?? true,
      powerLimitW: ch.maxMilliwatts ? Math.round(ch.maxMilliwatts / 1000) : 0,
      dmxUniverse: ch.dmxUniverse ?? 0,
      dmxStartChannel: ch.dmxStartChannel ?? 0,
      dmxMode: ch.dmxMode || 0,
      groupBits: gBits,
      pwmLevel: pwmLevels[ch.index] ?? 0,
    };
  }

  function closeConfigModal() {
    selectedChannel = null;
  }

  async function saveChannelConfig() {
    if (!isAdmin) {
      toastMsg('Admin privileges required to modify channel configuration');
      return;
    }
    if (!selectedChannel || !nodeIp || !config) return;
    savingChannel = true;
    try {
      // Ensure config.channels array has 16 items
      if (!config.channels || config.channels.length < 16) {
        config.channels = Array.from({ length: 16 }, (_, i) => ({
          name: '',
          enabled: false,
          addressableType: 0,
          pixelOrder: 1,
          pixelCount: 0,
          maxBrightness: 255,
          gammaCorrection: true,
          maxMilliwatts: 0,
          dmxUniverse: 0,
          dmxStartChannel: 0,
          dmxMode: 0,
        }));
      }

      // Map string pixel order to enum value
      const pixelOrderMap: Record<string, number> = { RGB: 0, GRB: 1, RGBW: 2, GRBW: 3 };
      const orderNum = pixelOrderMap[editForm.pixelOrder] ?? 1;
      const typeNum = editForm.addressableType === 'SK6812' ? 1 : 0;

      const idx = selectedChannel.index;
      const pixelCount = idx < 8
        ? Math.max(0, Math.min(1024, Math.trunc(editForm.pixelCount || 0)))
        : 0;

      if (idx < 8 && editForm.enabled && pixelCount < 1) {
        toastMsg(`Set a pixel count before enabling ${selectedChannel.port}`);
        savingChannel = false;
        return;
      }

      config.channels[idx] = {
        name: editForm.name.slice(0, 23),
        enabled: editForm.enabled,
        addressableType: typeNum,
        pixelOrder: orderNum,
        pixelCount,
        maxBrightness: Math.max(0, Math.min(255, editForm.maxBrightness)),
        gammaCorrection: editForm.gammaCorrection,
        maxMilliwatts: Math.max(0, editForm.powerLimitW * 1000),
        dmxUniverse: Math.max(0, Math.min(63999, Math.trunc(editForm.dmxUniverse || 0))),
        dmxStartChannel: Math.max(0, Math.min(512, Math.trunc(editForm.dmxStartChannel || 0))),
        dmxMode: editForm.dmxMode,
      };

      // Ensure config.groups array has 8 items
      if (!config.groups || config.groups.length < 8) {
        config.groups = Array.from({ length: 8 }, (_, i) => ({
          name: `Group ${i + 1}`,
          channelMask: 0,
          role: 0,
          enabled: false,
        }));
      }

      // Update group masks
      for (let g = 0; g < 8; g++) {
        let mask = config.groups[g].channelMask;
        if (editForm.groupBits[g]) {
          mask |= (1 << idx);
        } else {
          mask &= ~(1 << idx);
        }
        config.groups[g].channelMask = mask;
        config.groups[g].enabled = mask !== 0;
      }

      await saveDeviceConfig(nodeIp, config, config.checksum);
      toastMsg(`Channel ${selectedChannel.port} saved`);
      closeConfigModal();
      await loadData(nodeIp, loadGen);
    } catch (err) {
      toastMsg(`Save error: ${String(err)}`);
    } finally {
      savingChannel = false;
    }
  }

  function openGroupModal(idx: number) {
    if (!isAdmin) {
      toastMsg('Admin privileges required to edit groups');
      return;
    }
    const grp = config?.groups?.[idx];
    if (!grp) return;
    editingGroupIdx = idx;
    editingGroup = grp;

    const mask = grp.channelMask ?? 0;
    const chBools = Array.from({ length: 16 }, (_, i) => (mask & (1 << i)) !== 0);

    groupForm = {
      name: grp.name || `Group ${idx + 1}`,
      enabled: grp.enabled,
      channels: chBools,
    };
  }

  function closeGroupModal() {
    editingGroup = null;
    editingGroupIdx = -1;
  }

  async function saveGroupConfig() {
    if (!isAdmin) {
      toastMsg('Admin privileges required to modify groups');
      return;
    }
    if (!nodeIp || !config || editingGroupIdx < 0) return;
    savingGroup = true;

    try {
      if (!config.groups || config.groups.length < 8) {
        config.groups = Array.from({ length: 8 }, (_, i) => ({
          name: `Group ${i + 1}`,
          channelMask: 0,
          role: 0,
          enabled: false,
        }));
      }

      let newMask = 0;
      for (let i = 0; i < 16; i++) {
        if (groupForm.channels[i]) {
          newMask |= (1 << i);
        }
      }

      const existingRole = config.groups[editingGroupIdx]?.role ?? 0;
      config.groups[editingGroupIdx] = {
        name: groupForm.name.slice(0, 31).trim() || `Group ${editingGroupIdx + 1}`,
        channelMask: newMask,
        role: existingRole,
        enabled: groupForm.enabled && newMask > 0,
      };

      await saveDeviceConfig(nodeIp, config, config.checksum);
      toastMsg(`Group "${config.groups[editingGroupIdx].name}" saved`);
      closeGroupModal();
      await loadData(nodeIp, loadGen);
    } catch (err) {
      toastMsg(`Save error: ${String(err)}`);
    } finally {
      savingGroup = false;
    }
  }

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        loadGen++;
        channels = [];
        config = null;
        loading = !!nodeIp;
        error = '';
        if (nodeIp) loadData(nodeIp, loadGen);
      }
    });
    return unsub;
  });
</script>

<svelte:head><title>Channels — LumiGrid</title></svelte:head>

<div class="page">
  {#if toast}
    <div class="toast">{toast}</div>
  {/if}

  <header class="page-header">
    <div>
      <div class="title-row">
        <h1>Hardware Outputs</h1>
        <span class="board-badge">8 Addressable · 8 PWM</span>
      </div>
      <p class="sub">
        {nodeIp ? `Node: ${nodeIp}` : 'No node selected'}
        · Physical controller outputs · configure exactly what is connected
      </p>
    </div>
    <div class="header-actions">
      <button class="action-btn" onclick={() => loadData()} disabled={loading}>↺ Refresh</button>
    </div>
  </header>

  {#if !nodeIp}
    <div class="empty">Select an active node in the header or fleet list to inspect its physical connectors.</div>

  {:else if loading}
    <div class="loading"><div class="spinner"></div>Loading 16-channel hardware topology…</div>

  {:else if error}
    <div class="err-box">
      <strong>Failed to load channels:</strong> {error}
      <br><small>Ensure the selected node is online and reachable, then refresh.</small>
    </div>

  {:else}
    <!-- BANK 1: ADDRESSABLE CHANNELS (A1–A8) -->
    <section class="bank-section">
      <div class="bank-header">
        <div class="bank-title-group">
          <span class="bank-dot addr"></span>
          <h2>ADDRESSABLE OUTPUTS</h2>
          <span class="bank-count">{addressableActiveCount} / 8 active</span>
        </div>
        <span class="bank-hint">Digital LED strips · WS2812 / SK6812</span>
      </div>

      <div class="connector-grid">
        {#each addressableChannels as ch (ch.index)}
          {@const isTesting = testingChannel === ch.index}
          {@const statusText = !ch.configured ? 'UNUSED' : ch.enabled ? 'ACTIVE' : 'DISABLED'}
          {@const statusClass = !ch.configured ? 'unused' : ch.enabled ? 'online' : 'disabled'}
          <div
            class="connector-card addr"
            class:is-active={ch.enabled}
            class:is-testing={isTesting}
            onclick={() => openConfigModal(ch)}
            role="button"
            tabindex="0"
            onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') openConfigModal(ch); }}
          >
            <div class="card-top">
              <span class="port-id">{ch.port}</span>
              <span class="status-pill {statusClass}">{statusText}</span>
            </div>

            <div class="card-body">
              <div class="ch-name" title={ch.name || 'Unassigned'}>
                {ch.name || 'Unassigned'}
              </div>
              <div class="ch-meta">
                {#if ch.configured && ch.addressable}
                  <span class="strip-type">{ch.addressable.type}</span>
                  <span class="pixel-count">{ch.addressable.pixels} px</span>
                {:else}
                  <span class="setup-hint">Setup connector</span>
                {/if}
              </div>
            </div>

            <div class="card-footer">
              <span class="hw-spec">Addressable data output</span>
              <button
                class="test-btn"
                class:flashing={isTesting}
                onclick={(e) => runIdentify(ch, e)}
                disabled={isTesting}
                title="Flash strip red/green/blue/white for physical identification"
              >
                {isTesting ? 'FLASHING…' : 'IDENTIFY'}
              </button>
            </div>
          </div>
        {/each}
      </div>
    </section>

    <!-- BANK 2: PWM CHANNELS (P1–P8) -->
    <section class="bank-section">
      <div class="bank-header">
        <div class="bank-title-group">
          <span class="bank-dot pwm"></span>
          <h2>PWM CHANNELS</h2>
          <span class="bank-count">{pwmActiveCount} / 8 active</span>
        </div>
        <span class="bank-hint">Independent 12-bit dimmer outputs</span>
      </div>

      <div class="connector-grid">
        {#each pwmChannels as ch (ch.index)}
          {@const isTesting = testingChannel === ch.index}
          {@const statusText = !ch.configured ? 'UNUSED' : ch.enabled ? 'ACTIVE' : 'DISABLED'}
          {@const statusClass = !ch.configured ? 'unused' : ch.enabled ? 'online' : 'disabled'}
          {@const level = pwmLevels[ch.index] ?? 0}
          <div
            class="connector-card pwm"
            class:is-active={ch.enabled}
            class:is-testing={isTesting}
            onclick={() => openConfigModal(ch)}
            role="button"
            tabindex="0"
            onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') openConfigModal(ch); }}
          >
            <div class="card-top">
              <span class="port-id pwm">{ch.port}</span>
              <span class="status-pill {statusClass}">{statusText}</span>
            </div>

            <div class="card-body">
              <div class="ch-name" title={ch.name || 'Unassigned'}>
                {ch.name || '—'}
              </div>

              {#if ch.configured || ch.enabled}
                <div class="slider-block" onclick={(e) => e.stopPropagation()} role="presentation">
                  <div class="slider-info">
                    <span class="slider-label">Dimmer</span>
                    <span class="slider-val">{level}%</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="100"
                    value={level}
                    oninput={(e) => handlePwmSliderInput(ch, Number((e.target as HTMLInputElement).value))}
                    onchange={(e) => handlePwmSliderCommit(ch, Number((e.target as HTMLInputElement).value))}
                    class="pwm-slider"
                  />
                  <div class="pwm-bar-track">
                    <div class="pwm-bar-fill" style:width={`${level}%`}></div>
                  </div>
                </div>
              {:else}
                <div class="ch-meta">
                  <span class="setup-hint">Setup dimmer</span>
                </div>
              {/if}
            </div>

            <div class="card-footer">
              <span class="hw-spec">12-bit PWM output</span>
              <button
                class="test-btn"
                class:flashing={isTesting}
                onclick={(e) => runIdentify(ch, e)}
                disabled={isTesting}
                title="Step PWM duty cycle (0% -> 25% -> 50% -> 100% -> 0%)"
              >
                {isTesting ? 'TESTING…' : 'TEST'}
              </button>
            </div>
          </div>
        {/each}
      </div>
    </section>

    <!-- BANK 3: CHANNEL GROUPS (G1–G8) -->
    <section class="bank-section groups-bank">
      <div class="bank-header">
        <div class="bank-title-group">
          <span class="bank-dot grp-dot"></span>
          <h2>CHANNEL GROUPS</h2>
          <span class="bank-count">{activeGroupCount} / 8 active</span>
        </div>
        <span class="bank-hint">Logical target groups · Up to 8 groups</span>
      </div>

      <div class="connector-grid">
        {#each displayGroups as grp (grp.index)}
          <div
            class="connector-card grp-connector-card"
            class:is-active={grp.enabled}
            class:is-admin-card={isAdmin}
          >
            <div class="card-top">
              <span class="port-id grp-port-id">G{grp.index + 1}</span>
              <span class="status-pill" class:online={grp.enabled} class:disabled={!grp.enabled}>
                {grp.enabled ? 'ACTIVE' : 'DISABLED'}
              </span>
            </div>

            <div class="card-body">
              <div class="ch-name" title={grp.name}>
                {grp.name}
              </div>
              <div class="grp-members-block">
                <span class="grp-member-count-label">{grp.memberCount} channel{grp.memberCount === 1 ? '' : 's'}</span>
                {#if grp.memberChannels.length > 0}
                  <div class="grp-chips-flow">
                    {#each grp.memberChannels as port}
                      <span class="grp-port-tag" class:pwm-port-tag={port.startsWith('P')}>
                        {port}
                      </span>
                    {/each}
                  </div>
                {:else}
                  <span class="setup-hint">No channels assigned</span>
                {/if}
              </div>
            </div>

            <div class="card-footer">
              <span class="hw-spec">Mask: 0x{grp.channelMask.toString(16).toUpperCase().padStart(4, '0')}</span>
              {#if isAdmin}
                <button
                  type="button"
                  class="action-btn grp-edit-action"
                  onclick={(e) => { e.stopPropagation(); openGroupModal(grp.index); }}
                >
                  EDIT
                </button>
              {:else}
                <span class="grp-operator-readonly">View only</span>
              {/if}
            </div>
          </div>
        {/each}
      </div>
    </section>
  {/if}

  <!-- CHANNEL CONFIGURATION MODAL -->
  {#if selectedChannel}
    <div class="modal-backdrop" onclick={closeConfigModal} role="presentation">
      <div
        class="modal-content"
        onclick={(e) => e.stopPropagation()}
        onkeydown={(e) => { if (e.key === 'Escape') closeConfigModal(); }}
        role="dialog"
        tabindex="-1"
        aria-modal="true"
        aria-labelledby="modal-title"
      >
        <div class="modal-header">
          <div>
            <div class="modal-pretitle">
              {selectedChannel.kind === 'addressable' ? 'Addressable Output' : 'PWM Dimmer Output'}
            </div>
            <h2 id="modal-title" class="modal-title">
              <span class="modal-port-badge" class:pwm={selectedChannel.kind === 'pwm'}>
                {selectedChannel.port}
              </span>
              {editForm.name || `Channel ${selectedChannel.port}`}
            </h2>
          </div>
          <button class="modal-close-btn" onclick={closeConfigModal} aria-label="Close modal">✕</button>
        </div>

        <div class="modal-body">
          {#if !isAdmin}
            <div class="readonly-banner">
              <span>View-only mode · Admin role required to modify hardware configuration</span>
            </div>
          {/if}
          <fieldset disabled={!isAdmin} class="modal-fieldset">
          <!-- Status & Basic Information -->
          <div class="form-row">
            <label class="form-field toggle-field">
              <span class="field-label">Channel Status</span>
              <div class="toggle-wrap">
                <input type="checkbox" bind:checked={editForm.enabled} />
                <span class="toggle-text">{editForm.enabled ? 'Enabled' : 'Disabled'}</span>
              </div>
            </label>

            <label class="form-field flex-2">
              <span class="field-label">Channel Name</span>
              <input
                type="text"
                maxlength="23"
                placeholder="e.g. Front Logo, Window, Shelf"
                bind:value={editForm.name}
                class="text-input"
              />
            </label>
          </div>

          <!-- Kind-specific configuration -->
          {#if selectedChannel.kind === 'addressable'}
            <div class="section-divider">
              <span>LED Strip Parameters</span>
            </div>

            <div class="form-row">
              <label class="form-field">
                <span class="field-label">Strip Type</span>
                <select bind:value={editForm.addressableType} class="select-input">
                  <option value="WS2812">WS2812 / WS2811 (RGB)</option>
                  <option value="SK6812">SK6812 (RGBW)</option>
                </select>
              </label>

              <label class="form-field">
                <span class="field-label">Pixel Count</span>
                <input
                  type="number"
                  min="0"
                  max="1024"
                  placeholder="0 = unconfigured"
                  bind:value={editForm.pixelCount}
                  class="text-input"
                />
                <span class="input-hint">Set the connected strip length. 0 keeps this port unconfigured.</span>
              </label>

              <label class="form-field">
                <span class="field-label">Pixel Color Order</span>
                <select bind:value={editForm.pixelOrder} class="select-input">
                  <option value="GRB">GRB (Standard WS2812)</option>
                  <option value="RGB">RGB</option>
                  <option value="GRBW" disabled={editForm.addressableType === 'WS2812'} title={editForm.addressableType === 'WS2812' ? 'GRBW requires SK6812 hardware' : ''}>GRBW (SK6812 only)</option>
                  <option value="RGBW" disabled={editForm.addressableType === 'WS2812'} title={editForm.addressableType === 'WS2812' ? 'RGBW requires SK6812 hardware' : ''}>RGBW (SK6812 only)</option>
                </select>
                {#if editForm.addressableType === 'WS2812' && (editForm.pixelOrder === 'GRBW' || editForm.pixelOrder === 'RGBW')}
                  <span class="input-hint input-warn">⚠ RGBW/GRBW requires SK6812. Switch to GRB or RGB for this strip type.</span>
                {/if}
              </label>
            </div>
          {:else}
            <div class="section-divider">
              <span>Dimmer Parameters</span>
            </div>

            <div class="form-row">
              <label class="form-field">
                <span class="field-label">Gamma Correction</span>
                <div class="toggle-wrap">
                  <input type="checkbox" bind:checked={editForm.gammaCorrection} />
                  <span class="toggle-text">{editForm.gammaCorrection ? 'On (Perceptual)' : 'Off (Linear)'}</span>
                </div>
              </label>

              <label class="form-field flex-2">
                <span class="field-label">Live Dimmer Level ({editForm.pwmLevel}%)</span>
                <input
                  type="range"
                  min="0"
                  max="100"
                  bind:value={editForm.pwmLevel}
                  oninput={() => handlePwmSliderInput(selectedChannel!, editForm.pwmLevel)}
                  onchange={() => handlePwmSliderCommit(selectedChannel!, editForm.pwmLevel)}
                  class="pwm-slider"
                />
              </label>
            </div>
          {/if}

          <!-- Limits & Calibration -->
          <div class="section-divider">
            <span>Power & Limits</span>
          </div>

          <div class="form-row">
            <label class="form-field">
              <span class="field-label">
                Max Brightness&nbsp;<span class="field-subvalue">{Math.round((editForm.maxBrightness / 255) * 100)}% &bull; raw&nbsp;{editForm.maxBrightness}/255</span>
              </span>
              <input
                type="range"
                min="0"
                max="255"
                bind:value={editForm.maxBrightness}
                class="range-slider"
              />
            </label>

            <label class="form-field">
              <span class="field-label">Power Limit (Watts)</span>
              <input
                type="number"
                min="0"
                max="300"
                placeholder="0 = unlimited"
                bind:value={editForm.powerLimitW}
                class="text-input"
              />
              <span class="input-hint">{editForm.powerLimitW > 0 ? `${editForm.powerLimitW * 1000} mW cap` : 'No power cap'}</span>
            </label>
          </div>

          <!-- DMX Patching -->
          <div class="section-divider">
            <span>DMX Patching</span>
          </div>

          <div class="form-row">
            <label class="form-field">
              <span class="field-label">DMX Universe</span>
              <input
                type="number"
                min="0"
                max="63999"
                bind:value={editForm.dmxUniverse}
                class="text-input"
              />
            </label>

            <label class="form-field">
              <span class="field-label">{selectedChannel.kind === 'pwm' ? 'DMX Channel' : 'DMX Start Channel'}</span>
              <input
                type="number"
                min="0"
                max="512"
                bind:value={editForm.dmxStartChannel}
                class="text-input"
              />
              <span class="input-hint">0 = unpatched</span>
            </label>

            {#if selectedChannel.kind === 'addressable'}
              <label class="form-field">
                <span class="field-label">DMX Mode</span>
                <select bind:value={editForm.dmxMode} class="select-input">
                  <option value={0}>Wash (3/4 slots total)</option>
                  <option value={1}>Direct Pixels (Per-pixel RGB/W)</option>
                </select>
              </label>
            {/if}
          </div>

          <!-- Group Memberships -->
          <div class="section-divider">
            <span>Group Memberships</span>
          </div>

          <p class="group-hint">Select which groups contain this channel for bulk control:</p>
          <div class="group-grid">
            {#each Array(8) as _, g}
              {@const grpName = config?.groups?.[g]?.name || `Group ${g + 1}`}
              <label class="group-checkbox-card" class:checked={editForm.groupBits[g]}>
                <input
                  type="checkbox"
                  bind:checked={editForm.groupBits[g]}
                />
                <span class="grp-label">{grpName}</span>
              </label>
            {/each}
          </div>

          <!-- Fixed physical identity -->
          <div class="section-divider">
            <span>Connector Identity</span>
          </div>

          <div class="hw-readout">
            <div class="hw-item">
              <span class="hw-k">Physical Port</span>
              <span class="hw-v">{selectedChannel.port}</span>
            </div>
            <div class="hw-item">
              <span class="hw-k">Output Type</span>
              <span class="hw-v">{selectedChannel.kind === 'addressable' ? 'Addressable LED' : 'PWM Dimmer'}</span>
            </div>
            <div class="hw-item">
              <span class="hw-k">Controller Mapping</span>
              <span class="hw-v">Fixed · not editable</span>
            </div>
          </div>
          </fieldset>
        </div>

        <div class="modal-footer">
          <button
            class="action-btn test-action"
            onclick={() => runIdentify(selectedChannel!)}
            disabled={testingChannel !== null}
          >
            {testingChannel === selectedChannel.index ? 'TESTING…' : selectedChannel.kind === 'addressable' ? 'IDENTIFY' : 'TEST'}
          </button>
          <div class="modal-right-btns">
            <button class="action-btn cancel-btn" onclick={closeConfigModal}>Cancel</button>
            {#if isAdmin}
              <button class="primary-btn" onclick={saveChannelConfig} disabled={savingChannel}>
                {savingChannel ? 'Saving…' : 'Save Channel'}
              </button>
            {:else}
              <span class="admin-only-notice">Admin required to save</span>
            {/if}
          </div>
        </div>
      </div>
    </div>
  {/if}

  <!-- GROUP CONFIGURATION MODAL -->
  {#if editingGroup && editingGroupIdx >= 0}
    <div class="modal-backdrop" onclick={closeGroupModal} role="presentation">
      <div
        class="modal-content"
        onclick={(e) => e.stopPropagation()}
        onkeydown={(e) => { if (e.key === 'Escape') closeGroupModal(); }}
        role="dialog"
        tabindex="-1"
        aria-modal="true"
        aria-labelledby="grp-modal-title"
      >
        <div class="modal-header">
          <div>
            <div class="modal-pretitle">Channel Group Configuration</div>
            <h2 id="grp-modal-title" class="modal-title">
              <span class="modal-port-badge grp-badge">G{editingGroupIdx + 1}</span>
              {groupForm.name}
            </h2>
          </div>
          <button class="modal-close-btn" onclick={closeGroupModal} aria-label="Close modal">✕</button>
        </div>

        <div class="modal-body">
          <div class="form-row">
            <label class="form-field toggle-field">
              <span class="field-label">Group Status</span>
              <div class="toggle-wrap">
                <input type="checkbox" bind:checked={groupForm.enabled} />
                <span class="toggle-text">{groupForm.enabled ? 'Enabled' : 'Disabled'}</span>
              </div>
            </label>

            <label class="form-field flex-2">
              <span class="field-label">Group Name</span>
              <input
                type="text"
                maxlength="31"
                placeholder="e.g. Sign Face, Ambient"
                bind:value={groupForm.name}
                class="text-input"
              />
            </label>
          </div>

          <div class="section-divider">
            <span>Channel Membership</span>
          </div>

          <!-- Addressable selection (A1..A8) -->
          <div class="membership-group">
            <span class="membership-heading">Addressable Channels (A1–A8)</span>
            <div class="membership-list">
              {#each Array(8) as _, i}
                <label class="member-checkbox-wrap">
                  <input
                    type="checkbox"
                    bind:checked={groupForm.channels[i]}
                  />
                  <span class="member-tag-a">A{i + 1}</span>
                  <span class="member-subname">{channels[i]?.name || ''}</span>
                </label>
              {/each}
            </div>
          </div>

          <!-- PWM selection (P1..P8) -->
          <div class="membership-group">
            <span class="membership-heading">PWM Channels (P1–P8)</span>
            <div class="membership-list">
              {#each Array(8) as _, i}
                <label class="member-checkbox-wrap">
                  <input
                    type="checkbox"
                    bind:checked={groupForm.channels[i + 8]}
                  />
                  <span class="member-tag-p">P{i + 1}</span>
                  <span class="member-subname">{channels[i + 8]?.name || ''}</span>
                </label>
              {/each}
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <div></div>
          <div class="modal-right-btns">
            <button class="action-btn cancel-btn" onclick={closeGroupModal}>Cancel</button>
            <button class="primary-btn" onclick={saveGroupConfig} disabled={savingGroup}>
              {savingGroup ? 'Saving…' : 'Save Group'}
            </button>
          </div>
        </div>
      </div>
    </div>
  {/if}
</div>

<style>
  .bank-section {
    position: relative;
    overflow: hidden;
    background: linear-gradient(180deg, rgba(29,29,31,.72), rgba(18,18,20,.74));
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-xl);
    padding: 1rem;
    box-shadow: var(--dash-shadow);
  }
  .bank-section::before {
    content: "";
    position: absolute;
    inset: 0 0 auto;
    height: 1px;
    background: linear-gradient(90deg, rgba(var(--dash-accent-rgb),.55), transparent 42%);
    pointer-events: none;
  }
  .bank-section + .bank-section::before {
    background: linear-gradient(90deg, color-mix(in oklab, var(--dash-warm) 60%, transparent), transparent 42%);
  }
  .connector-card {
    isolation: isolate;
    min-height: 172px;
    background: linear-gradient(180deg, rgba(255,255,255,.038), rgba(255,255,255,.018));
    box-shadow: inset 0 1px 0 rgba(255,255,255,.035);
    transition: transform .18s var(--ease-out), border-color .18s ease, background .18s ease, box-shadow .18s ease;
  }
  .connector-card::before {
    content: "";
    position: absolute;
    inset: 0 auto 0 0;
    width: 3px;
    border-radius: inherit;
    opacity: .35;
    background: var(--dash-accent);
    pointer-events: none;
  }
  .connector-card.pwm::before { background: var(--dash-warm); }
  @media (hover:hover) {
    .connector-card:hover {
      transform: translateY(-2px);
      box-shadow: var(--dash-shadow-hover), inset 0 1px 0 rgba(255,255,255,.05);
      background: linear-gradient(180deg, rgba(255,255,255,.055), rgba(255,255,255,.024));
    }
    .connector-card.addr:hover { border-color: rgba(var(--dash-accent-rgb),.32); }
    .connector-card.pwm:hover { border-color: color-mix(in oklab, var(--dash-warm) 38%, transparent); }
  }
  .connector-card:focus-visible {
    outline: 2px solid var(--dash-accent);
    outline-offset: 3px;
  }
  .connector-card.pwm:focus-visible { outline-color: var(--dash-warm); }
  .ch-name {
    min-height: 1.35rem;
    color: var(--dash-text);
  }
  .connector-card:not(.is-active) .ch-name { color: #8e8e93; }
  .setup-hint {
    display: inline-flex;
    align-items: center;
    gap: .35rem;
  }
  .setup-hint::before { content: "+"; font-weight: 800; color: currentColor; }
  .hw-spec { opacity: .7; }
  .card-footer { border-top: 1px solid rgba(255,255,255,.055); padding-top: .7rem; }

  .page {
    max-width: 1180px;
    margin: 0 auto;
  }

  .page-header {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    margin-bottom: 2rem;
    gap: 1rem;
    flex-wrap: wrap;
  }

  .title-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex-wrap: wrap;
  }

  h1 {
    margin: 0;
    font-size: 1.75rem;
    font-weight: 700;
    color: #f5f5f7;
    letter-spacing: -0.02em;
  }

  .board-badge {
    font-size: 0.7rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #30d158;
    background: rgba(48, 209, 88, 0.12);
    border: 1px solid rgba(48, 209, 88, 0.25);
    padding: 0.2rem 0.6rem;
    border-radius: 999px;
  }

  .sub {
    margin: 0.35rem 0 0;
    color: #86868b;
    font-size: 0.88rem;
  }

  .action-btn {
    background: rgba(255, 255, 255, 0.08);
    border: 1px solid rgba(255, 255, 255, 0.14);
    border-radius: 999px;
    color: #f5f5f7;
    padding: 0.5rem 1.1rem;
    cursor: pointer;
    font-size: 0.85rem;
    font-weight: 500;
    transition: all 0.15s ease;
  }

  .action-btn:hover {
    background: rgba(255, 255, 255, 0.15);
    border-color: rgba(255, 255, 255, 0.25);
  }

  .action-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }

  .primary-btn {
    background: var(--dash-accent);
    border: 1px solid var(--dash-accent-hover);
    border-radius: 999px;
    color: #ffffff;
    padding: 0.5rem 1.4rem;
    cursor: pointer;
    font-size: 0.85rem;
    font-weight: 600;
    transition: all 0.15s ease;
  }

  .primary-btn:hover {
    background: var(--dash-accent-hover);
  }

  .primary-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }

  .toast {
    position: fixed;
    top: 1.5rem;
    right: 2rem;
    background: rgba(29, 29, 31, 0.95);
    backdrop-filter: blur(20px);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.4);
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
    color: #f5f5f7;
    padding: 0.75rem 1.25rem;
    border-radius: 12px;
    font-size: 0.88rem;
    font-weight: 500;
    z-index: 2000;
  }

  .loading {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    color: #86868b;
    padding: 4rem;
    justify-content: center;
    font-size: 0.95rem;
  }

  .spinner {
    width: 20px;
    height: 20px;
    border: 2px solid rgba(255, 255, 255, 0.1);
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }

  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  .empty {
    color: #86868b;
    padding: 4rem 2rem;
    text-align: center;
    font-size: 0.95rem;
  }

  .err-box {
    background: rgba(255, 69, 58, 0.1);
    border: 1px solid rgba(255, 69, 58, 0.25);
    border-radius: 14px;
    color: #ff453a;
    padding: 1.25rem 1.5rem;
    font-size: 0.9rem;
    line-height: 1.6;
    margin-bottom: 2rem;
  }

  /* Hardware Banks */
  .bank-section {
    position: relative;
    overflow: hidden;
    background: linear-gradient(180deg, rgba(29,29,31,.76), rgba(16,16,18,.76));
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-xl);
    padding: 1.35rem;
    margin-bottom: 1.25rem;
    box-shadow: var(--dash-shadow);
  }

  .bank-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 1.25rem;
    flex-wrap: wrap;
    gap: 0.75rem;
  }

  .bank-title-group {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }

  .bank-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
  }

  .bank-dot.addr {
    background: var(--dash-accent);
    box-shadow: 0 0 10px rgba(var(--dash-accent-rgb), 0.6);
  }

  .bank-dot.pwm {
    background: #ff9f0a;
    box-shadow: 0 0 10px rgba(255, 159, 10, 0.6);
  }

  h2 {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 700;
    color: #f5f5f7;
    letter-spacing: -0.01em;
  }

  .bank-count {
    font-size: 0.75rem;
    font-weight: 600;
    color: #a1a1a6;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.08);
    padding: 0.2rem 0.6rem;
    border-radius: 999px;
  }

  .bank-hint {
    font-size: 0.8rem;
    color: #86868b;
  }

  /* Connector Grid */
  .connector-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(230px, 1fr));
    gap: 1rem;
  }

  .connector-card {
    isolation: isolate;
    min-height: 172px;
    background: linear-gradient(180deg, rgba(255,255,255,.04), rgba(255,255,255,.018));
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: var(--dash-radius-md);
    padding: 1.1rem;
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
    cursor: pointer;
    transition: transform .18s var(--ease-out), border-color .18s ease, background .18s ease, box-shadow .18s ease;
    position: relative;
    user-select: none;
    box-shadow: inset 0 1px 0 rgba(255,255,255,.035);
  }

  .connector-card:hover {
    background: linear-gradient(180deg, rgba(255,255,255,.06), rgba(255,255,255,.026));
    border-color: rgba(255, 255, 255, 0.18);
    transform: translateY(-2px);
    box-shadow: var(--dash-shadow-hover), inset 0 1px 0 rgba(255,255,255,.05);
  }
  .connector-card:focus-visible {
    outline: 2px solid var(--dash-accent);
    outline-offset: 3px;
  }
  .connector-card.pwm:focus-visible { outline-color: var(--dash-warm); }

  .connector-card.is-active.addr {
    border-color: rgba(var(--dash-accent-rgb), 0.35);
    background: rgba(var(--dash-accent-rgb), 0.04);
  }

  .connector-card.is-active.pwm {
    border-color: rgba(255, 159, 10, 0.35);
    background: rgba(255, 159, 10, 0.04);
  }

  .connector-card.is-testing {
    animation: cardPulse 0.5s ease infinite alternate;
  }

  @keyframes cardPulse {
    from { border-color: rgba(255, 255, 255, 0.2); }
    to { border-color: var(--dash-accent); box-shadow: 0 0 16px rgba(var(--dash-accent-rgb), 0.4); }
  }

  .card-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .port-id {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    font-size: 1.1rem;
    font-weight: 800;
    color: var(--dash-accent);
    letter-spacing: -0.02em;
  }

  .port-id.pwm {
    color: #ff9f0a;
  }

  .status-pill {
    font-size: 0.65rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    padding: 0.15rem 0.5rem;
    border-radius: 999px;
    text-transform: uppercase;
  }

  .status-pill.online {
    background: rgba(48, 209, 88, 0.15);
    color: #30d158;
    border: 1px solid rgba(48, 209, 88, 0.3);
  }

  .status-pill.disabled {
    background: rgba(255, 159, 10, 0.12);
    color: #ff9f0a;
    border: 1px solid rgba(255, 159, 10, 0.25);
  }

  .status-pill.unused {
    background: rgba(255, 255, 255, 0.06);
    color: #636366;
    border: 1px solid rgba(255, 255, 255, 0.06);
  }

  .card-body {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  .ch-name {
    min-height: 1.35rem;
    font-size: 0.92rem;
    font-weight: 600;
    color: #f5f5f7;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .ch-meta {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.78rem;
  }

  .strip-type {
    background: rgba(255, 255, 255, 0.07);
    color: #e5e5ea;
    padding: 0.1rem 0.45rem;
    border-radius: 4px;
    font-weight: 500;
  }

  .pixel-count {
    color: #86868b;
    font-family: monospace;
  }

  .setup-hint {
    color: #7c7c80;
    font-size: 0.8rem;
    font-style: normal;
    display: inline-flex;
    align-items: center;
    gap: .35rem;
  }
  .setup-hint::before {
    content: "+";
    color: currentColor;
    font-weight: 800;
  }

  .slider-block {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    margin-top: 0.2rem;
  }

  .slider-info {
    display: flex;
    justify-content: space-between;
    font-size: 0.75rem;
  }

  .slider-label {
    color: #86868b;
  }

  .slider-val {
    color: #f5f5f7;
    font-family: monospace;
    font-weight: 600;
  }

  .pwm-slider {
    width: 100%;
    accent-color: #ff9f0a;
    cursor: pointer;
  }

  .pwm-bar-track {
    height: 3px;
    background: rgba(255, 255, 255, 0.06);
    border-radius: 999px;
    overflow: hidden;
  }

  .pwm-bar-fill {
    height: 100%;
    background: linear-gradient(90deg, #ff9f0a, #ff453a);
    transition: width 0.08s ease;
  }

  .card-footer {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding-top: 0.6rem;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
  }

  .hw-spec {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    font-size: 0.68rem;
    color: #86868b;
  }

  .test-btn {
    background: rgba(255, 255, 255, 0.08);
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #e5e5ea;
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    padding: 0.25rem 0.65rem;
    border-radius: 6px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .test-btn:hover {
    background: rgba(255, 255, 255, 0.18);
    color: #ffffff;
    border-color: rgba(255, 255, 255, 0.25);
  }

  .test-btn.flashing {
    background: #ff2d55;
    color: #ffffff;
    border-color: #ff375f;
    animation: flash 0.3s alternate infinite;
  }

  @keyframes flash {
    from { opacity: 1; }
    to { opacity: 0.4; }
  }

  /* Modal */
  .modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.75);
    backdrop-filter: blur(12px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1500;
    padding: 1rem;
  }

  .modal-content {
    background: var(--dash-surface-strong);
    backdrop-filter: blur(24px);
    -webkit-backdrop-filter: blur(24px);
    border: 1px solid rgba(255, 255, 255, 0.14);
    border-radius: var(--dash-radius-xl);
    width: 100%;
    max-width: 680px;
    max-height: 90vh;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    box-shadow: 0 24px 64px rgba(0, 0, 0, 0.8);
  }

  .modal-header {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    padding: 1.5rem 1.75rem 1rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }

  .modal-pretitle {
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #86868b;
    margin-bottom: 0.25rem;
  }

  .modal-title {
    margin: 0;
    font-size: 1.35rem;
    font-weight: 700;
    color: #f5f5f7;
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }

  .modal-port-badge {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    font-size: 0.95rem;
    font-weight: 800;
    color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.12);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.3);
    padding: 0.15rem 0.55rem;
    border-radius: 6px;
  }

  .modal-port-badge.pwm {
    color: #ff9f0a;
    background: rgba(255, 159, 10, 0.12);
    border-color: rgba(255, 159, 10, 0.3);
  }

  .modal-close-btn {
    background: rgba(255, 255, 255, 0.08);
    border: none;
    border-radius: 50%;
    width: 32px;
    height: 32px;
    color: #a1a1a6;
    cursor: pointer;
    font-size: 0.9rem;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all 0.15s ease;
  }

  .modal-close-btn:hover {
    background: rgba(255, 255, 255, 0.15);
    color: #f5f5f7;
  }

  .modal-body {
    padding: 1.5rem 1.75rem;
    display: flex;
    flex-direction: column;
    gap: 1.2rem;
  }

  .section-divider {
    display: flex;
    align-items: center;
    font-size: 0.78rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #86868b;
    margin-top: 0.5rem;
  }

  .section-divider::after {
    content: '';
    flex: 1;
    height: 1px;
    background: rgba(255, 255, 255, 0.08);
    margin-left: 0.75rem;
  }

  .form-row {
    display: flex;
    gap: 1rem;
    flex-wrap: wrap;
  }

  .form-field {
    flex: 1;
    min-width: 160px;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  .form-field.flex-2 {
    flex: 2;
  }

  .field-label {
    font-size: 0.78rem;
    font-weight: 600;
    color: #a1a1a6;
  }

  .input-hint {
    font-size: 0.7rem;
    color: #636366;
  }

  .input-hint.input-warn {
    color: #ff9f0a;
    font-weight: 500;
  }

  .field-subvalue {
    font-weight: 400;
    font-size: 0.72rem;
    color: #636366;
    font-variant-numeric: tabular-nums;
  }

  .text-input, .select-input {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    padding: 0.6rem 0.8rem;
    color: #f5f5f7;
    font-size: 0.88rem;
    transition: border-color .15s ease, box-shadow .15s ease;
  }

  .text-input:focus-visible, .select-input:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12);
  }

  .select-input option {
    background: #1c1c1e;
    color: #f5f5f7;
  }

  .toggle-field {
    justify-content: center;
  }

  .toggle-wrap {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-top: 0.2rem;
  }

  .toggle-text {
    font-size: 0.88rem;
    color: #f5f5f7;
  }

  .range-slider {
    accent-color: var(--dash-accent);
    cursor: pointer;
  }

  .group-hint {
    margin: 0;
    font-size: 0.8rem;
    color: #86868b;
  }

  .group-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(130px, 1fr));
    gap: 0.5rem;
  }

  .group-checkbox-card {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.5rem 0.75rem;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 8px;
    cursor: pointer;
    font-size: 0.82rem;
    color: #a1a1a6;
    transition: all 0.15s ease;
  }

  .group-checkbox-card.checked {
    background: rgba(var(--dash-accent-rgb), 0.1);
    border-color: rgba(var(--dash-accent-rgb), 0.35);
    color: #f5f5f7;
  }

  .group-checkbox-card input[type="checkbox"] {
    accent-color: var(--dash-accent);
  }

  .grp-label {
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .hw-readout {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 0.6rem;
    background: rgba(0, 0, 0, 0.25);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 10px;
    padding: 0.75rem 1rem;
  }

  .hw-item {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
  }

  .hw-k {
    font-size: 0.68rem;
    color: #636366;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .hw-v {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    font-size: 0.82rem;
    font-weight: 600;
    color: #e5e5ea;
  }

  .modal-footer {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 1.25rem 1.75rem;
    border-top: 1px solid rgba(255, 255, 255, 0.08);
    background: rgba(0, 0, 0, 0.15);
  }

  .modal-right-btns {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .test-action {
    background: rgba(255, 255, 255, 0.06);
    border-color: rgba(255, 255, 255, 0.12);
  }

  .cancel-btn {
    background: transparent;
    border-color: rgba(255, 255, 255, 0.12);
  }

  /* Group and Admin styles */
  .grp-dot {
    background: #d946ef;
    box-shadow: 0 0 10px rgba(217, 70, 239, 0.4);
  }

  .grp-port-id {
    color: #e879f9;
  }

  .grp-badge {
    color: #e879f9;
    background: rgba(217, 70, 239, 0.15);
    border-color: rgba(217, 70, 239, 0.3);
  }

  .grp-members-block {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    margin-top: 0.25rem;
  }

  .grp-member-count-label {
    font-size: 0.75rem;
    color: #a1a1aa;
    font-weight: 500;
  }

  .grp-chips-flow {
    display: flex;
    flex-wrap: wrap;
    gap: 0.25rem;
  }

  .grp-port-tag {
    font-size: 0.65rem;
    font-family: ui-monospace, monospace;
    font-weight: 700;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    background: rgba(217, 70, 239, 0.15);
    color: #e879f9;
    border: 1px solid rgba(217, 70, 239, 0.3);
  }

  .grp-port-tag.pwm-port-tag {
    background: rgba(245, 158, 11, 0.15);
    color: #fbbf24;
    border-color: rgba(245, 158, 11, 0.3);
  }

  .grp-edit-action {
    font-size: 0.75rem;
    padding: 0.25rem 0.65rem;
  }

  .grp-operator-readonly,
  .admin-only-notice {
    font-size: 0.75rem;
    color: #71717a;
  }

  .readonly-banner {
    padding: 0.5rem 0.75rem;
    background: rgba(245, 158, 11, 0.1);
    border: 1px solid rgba(245, 158, 11, 0.25);
    color: #fbbf24;
    font-size: 0.8rem;
    border-radius: 6px;
  }

  .modal-fieldset {
    border: none;
    padding: 0;
    margin: 0;
    display: flex;
    flex-direction: column;
    gap: 1.2rem;
  }

  .modal-fieldset:disabled {
    opacity: 0.75;
  }

  /* Membership in Modal */
  .membership-group {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    margin-top: 0.5rem;
  }

  .membership-heading {
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    color: #a1a1aa;
    letter-spacing: 0.05em;
  }

  .membership-list {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(110px, 1fr));
    gap: 0.4rem;
  }

  .member-checkbox-wrap {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0.35rem 0.5rem;
    border-radius: 4px;
    background: rgba(0, 0, 0, 0.2);
    border: 1px solid rgba(255, 255, 255, 0.06);
    cursor: pointer;
    font-size: 0.8rem;
  }

  .member-checkbox-wrap:hover {
    background: rgba(255, 255, 255, 0.05);
  }

  .member-tag-a {
    font-weight: 700;
    font-family: ui-monospace, monospace;
    color: #e879f9;
  }

  .member-tag-p {
    font-weight: 700;
    font-family: ui-monospace, monospace;
    color: #fbbf24;
  }

  .member-subname {
    font-size: 0.65rem;
    color: #71717a;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  @media (max-width: 720px) {
    .page-header { margin-bottom: 1.25rem; }
    .page-header .action-btn { width: 100%; }
    .bank-section { padding: 1rem; }
    .bank-header { align-items: flex-start; }
    .bank-hint { width: 100%; }
    .connector-grid { grid-template-columns: 1fr; }
    .connector-card { min-height: 150px; }
    .modal-backdrop {
      padding: 0;
      align-items: flex-end;
    }
    .modal-content {
      max-width: none;
      max-height: 92dvh;
      border-radius: 22px 22px 0 0;
      border-bottom: 0;
    }
    .modal-header { padding: 1.15rem 1rem .9rem; }
    .modal-body { padding: 1rem; }
    .modal-footer { padding: .9rem 1rem calc(.9rem + env(safe-area-inset-bottom)); }
    .form-row { flex-direction: column; gap: .8rem; }
    .form-field { min-width: 0; }
    .group-grid { grid-template-columns: 1fr 1fr; }
  }

</style>
