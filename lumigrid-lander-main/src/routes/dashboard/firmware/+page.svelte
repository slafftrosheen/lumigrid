<script lang="ts">
  import { onMount } from 'svelte';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import {
    getNodeInfo,
    uploadFirmware,
    getDeviceConfig,
    saveDeviceConfig,
    getBackup,
    restoreBackup,
    type PublicNodeInfo,
    type DeviceConfig,
  } from '$lib/api/node';
  import {
    getSelectedNodeIp,
    getSavedNodes,
    NODE_REGISTRY_CHANGED_EVENT,
  } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';
  import { executeStagedDeployment, type DeploymentPlan } from '$lib/api/deployment';

  type NodeOtaState = {
    ip: string;
    name: string;
    version: string;
    status: 'idle' | 'uploading' | 'verifying' | 'done' | 'failed' | 'rollback';
    progress: number;
    error: string;
  };

  let activeTab = $state<'config' | 'recovery' | 'ota'>('config');
  let selectedNodeIp = $state('');
  let loadingConfig = $state(false);
  let savingConfig = $state(false);
  let configError = $state('');
  let configToast = $state('');

  // Device Config states
  let currentConfig = $state<DeviceConfig | null>(null);
  let savedConfigSnapshot = $state<DeviceConfig | null>(null);

  const isConfigDirty = $derived(
    currentConfig !== null &&
    savedConfigSnapshot !== null &&
    JSON.stringify(currentConfig) !== JSON.stringify(savedConfigSnapshot)
  );

  // Recovery & Backup states
  let backupLoading = $state(false);
  let restoreLoading = $state(false);
  let restoreFile = $state<File | null>(null);
  let restorePayload = $state<Record<string, unknown> | null>(null);
  let restoreConfirmOpen = $state(false);

  // OTA states
  let nodeList = $state<string[]>([]);
  let nodes = $state<NodeOtaState[]>([]);
  let selected = $state<Set<string>>(new Set());
  let binFile = $state<File | null>(null);
  let binName = $state('');
  let prevBin = $state<File | null>(null);
  let runningOta = $state(false);
  let otaConfirmOpen = $state(false);

  const STATUS_LABEL: Record<string, string> = {
    idle: 'Ready',
    uploading: 'Uploading…',
    verifying: 'Rebooting…',
    done: '✓ Done',
    failed: '✗ Failed',
    rollback: 'Rolling back…',
  };

  const STATUS_COLOR: Record<string, string> = {
    idle: '#a1a1a6',
    uploading: '#00d4ff',
    verifying: '#ff9f0a',
    done: '#30d158',
    failed: '#ff453a',
    rollback: '#ff9f0a',
  };

  let loadGen = 0;

  async function loadConfigForNode(targetIp = selectedNodeIp, gen = loadGen) {
    if (!targetIp) {
      currentConfig = null;
      savedConfigSnapshot = null;
      loadingConfig = false;
      return;
    }
    loadingConfig = true;
    configError = '';
    try {
      const cfg = await getDeviceConfig(targetIp);
      if (gen !== loadGen) return;
      currentConfig = structuredClone(cfg);
      savedConfigSnapshot = structuredClone(cfg);
    } catch (e) {
      if (gen !== loadGen) return;
      configError = `Failed to load device config: ${e}`;
    } finally {
      if (gen === loadGen) {
        loadingConfig = false;
      }
    }
  }

  async function saveConfig() {
    if (!selectedNodeIp || !currentConfig || savingConfig) return;
    savingConfig = true;
    configError = '';
    try {
      await saveDeviceConfig(selectedNodeIp, currentConfig);
      const updated = await getDeviceConfig(selectedNodeIp);
      currentConfig = structuredClone(updated);
      savedConfigSnapshot = structuredClone(updated);
      configToastMsg('Configuration saved successfully to hardware');
    } catch (e) {
      configError = `Failed to save configuration: ${e}`;
    } finally {
      savingConfig = false;
    }
  }

  function revertConfig() {
    if (savedConfigSnapshot) {
      currentConfig = structuredClone(savedConfigSnapshot);
      configToastMsg('Reverted modifications to current saved config');
    }
  }

  async function downloadNodeBackup() {
    if (!selectedNodeIp || backupLoading) return;
    backupLoading = true;
    try {
      const backupData = await getBackup(selectedNodeIp);
      const blob = new Blob([JSON.stringify(backupData, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `lumigrid_backup_${selectedNodeIp.replace(/\./g, '_')}_${Date.now()}.json`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      configToastMsg('Backup package exported successfully');
    } catch (e) {
      configError = `Backup export failed: ${e}`;
    } finally {
      backupLoading = false;
    }
  }

  async function onRestoreFileSelected(e: Event) {
    const f = (e.target as HTMLInputElement).files?.[0];
    restoreFile = null;
    restorePayload = null;
    restoreConfirmOpen = false;
    if (!f) return;

    if (!f.name.toLowerCase().endsWith('.json')) {
      configError = 'Choose a LumiGrid backup JSON file.';
      return;
    }
    if (f.size <= 0) {
      configError = 'The selected backup file is empty.';
      return;
    }
    if (f.size > 4 * 1024 * 1024) {
      configError = 'Backup file is unexpectedly large. Refusing files over 4 MB.';
      return;
    }

    try {
      const parsed = JSON.parse(await f.text());
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
        throw new Error('backup root must be a JSON object');
      }
      restoreFile = f;
      restorePayload = parsed;
      configError = '';
    } catch (e) {
      configError = `Invalid backup JSON: ${e}`;
    }
  }

  async function executeRestore() {
    if (!selectedNodeIp || !restoreFile || restorePayload === null || restoreLoading) return;
    restoreLoading = true;
    try {
      await restoreBackup(selectedNodeIp, restorePayload);
      restoreConfirmOpen = false;
      restoreFile = null;
      restorePayload = null;
      configToastMsg('Backup restored successfully on node');
      await loadConfigForNode();
    } catch (e) {
      configError = `Restore failed: ${e}`;
    } finally {
      restoreLoading = false;
    }
  }

  // OTA Functions
  async function refreshVersions() {
    const infos = await Promise.allSettled(
      nodeList.map(async ip => ({ ip, info: await getNodeInfo(ip) }))
    );
    nodes = nodeList.map((ip, i) => {
      const r = infos[i];
      const info: PublicNodeInfo | null = r.status === 'fulfilled' ? r.value.info : null;
      return {
        ip,
        name: info?.name ?? ip,
        version: info?.firmwareVersion ?? '?',
        status: 'idle',
        progress: 0,
        error: r.status === 'rejected' ? String((r as PromiseRejectedResult).reason) : '',
      };
    });
  }

  function selectFirmwareFile(f: File | undefined) {
    otaConfirmOpen = false;
    if (!f) return;

    if (!f.name.toLowerCase().endsWith('.bin')) {
      binFile = null;
      binName = '';
      configError = 'Choose a firmware .bin file.';
      return;
    }
    if (f.size <= 0) {
      binFile = null;
      binName = '';
      configError = 'The selected firmware binary is empty.';
      return;
    }

    binFile = f;
    binName = f.name;
    configError = '';
  }

  function onFile(e: Event) {
    selectFirmwareFile((e.target as HTMLInputElement).files?.[0]);
  }

  function onDrop(e: DragEvent) {
    e.preventDefault();
    selectFirmwareFile(e.dataTransfer?.files[0]);
  }

  function toggleNode(ip: string) {
    const s = new Set(selected);
    if (s.has(ip)) s.delete(ip);
    else s.add(ip);
    selected = s;
  }

  function startOta() {
    if (!binFile || !selected.size || runningOta) return;
    otaConfirmOpen = true;
  }

  async function executeOta() {
    if (!binFile || !selected.size || runningOta) return;
    otaConfirmOpen = false;
    runningOta = true;
    prevBin = binFile;
    const selectedNodes = nodes.filter(n => selected.has(n.ip));
    const nodeMap = new Map<string, string>();
    const nodeIds: string[] = [];
    selectedNodes.forEach(n => {
      const id = n.name || n.ip;
      nodeIds.push(id);
      nodeMap.set(id, n.ip);
    });

    const plan: DeploymentPlan = {
      id: `dep-${Date.now()}`,
      targetVersion: '',
      nodeIds,
      canaryCount: 1,
      concurrency: 2,
      state: 'preflight',
    };

    configToastMsg(`Starting staged deployment: Preflight -> Canary -> Fleet rollout`);

    try {
      await executeStagedDeployment(plan, nodeMap, binFile, (prog) => {
        if (prog.canaryNodeId) {
          const cIp = nodeMap.get(prog.canaryNodeId);
          const cIdx = nodes.findIndex(n => n.ip === cIp);
          if (cIdx >= 0) {
            setNodeStatus(cIdx, prog.state === 'verifying' ? 'verifying' : 'uploading', 50, '');
          }
        }
        prog.activeNodeIds.forEach(id => {
          const aIp = nodeMap.get(id);
          const aIdx = nodes.findIndex(n => n.ip === aIp);
          if (aIdx >= 0) {
            setNodeStatus(aIdx, 'uploading', 50, '');
          }
        });
        prog.completedNodeIds.forEach(id => {
          const compIp = nodeMap.get(id);
          const compIdx = nodes.findIndex(n => n.ip === compIp);
          if (compIdx >= 0) {
            setNodeStatus(compIdx, 'done', 100, '');
          }
        });
        prog.failedNodeIds.forEach(id => {
          const fIp = nodeMap.get(id);
          const fIdx = nodes.findIndex(n => n.ip === fIp);
          if (fIdx >= 0) {
            setNodeStatus(fIdx, 'failed', 0, prog.error || 'Failed');
          }
        });
      });
      configToastMsg(`Staged deployment complete`);
    } catch (e) {
      configToastMsg(`Deployment error: ${e}`);
    } finally {
      runningOta = false;
      refreshVersions().catch(() => {});
    }
  }

  async function flashNode(node: NodeOtaState) {
    const idx = nodes.findIndex(n => n.ip === node.ip);
    if (idx < 0 || !binFile) return;
    setNodeStatus(idx, 'uploading', 0, '');

    try {
      await uploadFirmware(node.ip, binFile, p => setNodeStatus(idx, 'uploading', p, ''));
    } catch (e) {
      setNodeStatus(idx, 'failed', 0, String(e));
      return;
    }

    setNodeStatus(idx, 'verifying', 100, '');
    const oldVer = node.version;
    const t0 = Date.now();
    while (Date.now() - t0 < 60000) {
      await sleep(3000);
      try {
        const res = await getNodeInfo(node.ip);
        if (res.firmwareVersion && res.firmwareVersion !== oldVer) {
          nodes = nodes.map((n, i) =>
            i === idx ? { ...n, version: res.firmwareVersion, status: 'done', progress: 100, error: '' } : n
          );
          return;
        }
      } catch {
        // expected while node is rebooting
      }
    }
    setNodeStatus(idx, 'failed', 0, 'Verification timeout — check node manually');
  }

  function setNodeStatus(
    idx: number,
    status: NodeOtaState['status'],
    progress: number,
    error: string
  ) {
    nodes = nodes.map((n, i) => (i === idx ? { ...n, status, progress, error } : n));
  }

  async function rollback(node: NodeOtaState) {
    if (!prevBin) return;
    const idx = nodes.findIndex(n => n.ip === node.ip);
    setNodeStatus(idx, 'rollback', 0, '');
    try {
      await uploadFirmware(node.ip, prevBin, p => setNodeStatus(idx, 'rollback', p, ''));
      setNodeStatus(idx, 'idle', 0, '');
    } catch (e) {
      setNodeStatus(idx, 'failed', 0, `Rollback failed: ${e}`);
    }
  }

  function sleep(ms: number) {
    return new Promise(r => setTimeout(r, ms));
  }

  function configToastMsg(msg: string) {
    configToast = msg;
    setTimeout(() => {
      if (configToast === msg) configToast = '';
    }, 3000);
  }

  onMount(() => {
    const syncFleetTargets = (selectAll = false) => {
      const next = getSavedNodes().map(node => node.ip);
      nodeList = next;

      if (selectAll) {
        selected = new Set(next);
      } else {
        selected = new Set([...selected].filter(ip => next.includes(ip)));
      }

      refreshVersions().catch(() => {});
    };

    syncFleetTargets(true);

    const unsub = nodeStore.subscribe(state => {
      if (state.status?.job && selectedNodeIp) {
        const job = state.status.job;
        const idx = nodes.findIndex(n => n.ip === selectedNodeIp);
        if (idx >= 0) {
          let s: NodeOtaState['status'] = 'idle';
          if (job.state === 'running' || job.state === 'preparing') s = 'uploading';
          else if (job.state === 'verifying') s = 'verifying';
          else if (job.state === 'completed') s = 'done';
          else if (job.state === 'failed') s = 'failed';
          setNodeStatus(idx, s, job.progress, job.error || '');
        }
      }

      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== selectedNodeIp) {
        selectedNodeIp = targetIp;
        loadGen++;
        restoreConfirmOpen = false;
        restoreFile = null;
        restorePayload = null;
        if (selectedNodeIp) {
          currentConfig = null;
          savedConfigSnapshot = null;
          loadingConfig = true;
          loadConfigForNode(selectedNodeIp, loadGen);
        } else {
          currentConfig = null;
          savedConfigSnapshot = null;
          loadingConfig = false;
          configError = '';
        }
      }
    });

    const onRegistryChanged = () => syncFleetTargets(false);
    window.addEventListener(NODE_REGISTRY_CHANGED_EVENT, onRegistryChanged);

    return () => {
      unsub();
      window.removeEventListener(NODE_REGISTRY_CHANGED_EVENT, onRegistryChanged);
    };
  });
</script>

<svelte:head>
  <title>Configuration & Firmware — LumiGrid</title>
</svelte:head>

<div class="config-page">
  <!-- Page Header -->
  <header class="page-header">
    <div>
      <h1>Configuration & Firmware</h1>
      <p class="sub">Node: <span class="ip-tag">{selectedNodeIp || 'None selected'}</span></p>
    </div>

    <!-- Navigation Tabs -->
    <div class="tab-bar">
      <button
        class="tab"
        class:active={activeTab === 'config'}
        aria-pressed={activeTab === 'config'}
        onclick={() => (activeTab = 'config')}
        type="button"
      >
        Device Parameters
      </button>
      <button
        class="tab"
        class:active={activeTab === 'recovery'}
        aria-pressed={activeTab === 'recovery'}
        onclick={() => (activeTab = 'recovery')}
        type="button"
      >
        Backup & Recovery
      </button>
      <button
        class="tab"
        class:active={activeTab === 'ota'}
        aria-pressed={activeTab === 'ota'}
        onclick={() => (activeTab = 'ota')}
        type="button"
      >
        OTA Firmware Update
      </button>
    </div>
  </header>

  {#if configError}
    <div class="error-banner">{configError}</div>
  {/if}
  {#if configToast}
    <div class="toast-banner">{configToast}</div>
  {/if}

  <!-- TAB 1: DEVICE PARAMETERS -->
  {#if activeTab === 'config'}
    {#if loadingConfig}
      <div class="loading-box">
        <div class="spinner"></div>
        <p>Loading device configuration from {selectedNodeIp}…</p>
      </div>
    {:else if !selectedNodeIp}
      <div class="empty-box">
        <p>No node selected. Connect to a node in the header to manage configuration.</p>
      </div>
    {:else if currentConfig}
      <div class="config-container">
        <!-- Status & Action Bar -->
        <div class="config-toolbar">
          <div class="status-group">
            <span class="status-title">Config State:</span>
            {#if isConfigDirty}
              <span class="badge badge-edited">EDITING (UNSAVED)</span>
            {:else if savedConfigSnapshot}
              <span class="badge badge-saved">SAVED ON NODE</span>
            {/if}
          </div>

          <div class="config-actions">
            <button
              class="btn-secondary"
              onclick={revertConfig}
              disabled={!isConfigDirty}
              type="button"
            >
              Revert Changes
            </button>
            <button
              class="btn-primary"
              onclick={saveConfig}
              disabled={savingConfig || !isConfigDirty}
              type="button"
            >
              {savingConfig ? 'Saving...' : 'Save Configuration'}
            </button>
          </div>
        </div>

        <!-- Settings Cards Grid -->
        <div class="settings-grid">
          <!-- Identity & Naming -->
          <div class="settings-card">
            <h3>Device Identity</h3>
            <label class="field">
              <span>Device Name</span>
              <input type="text" maxlength={31} bind:value={currentConfig.deviceName} />
            </label>
            <label class="field">
              <span>Active Scene Index (0–31)</span>
              <input type="number" min="0" max="31" bind:value={currentConfig.activePresetIndex} />
            </label>
            <label class="field">
              <span>Fallback Scene Index (0–31)</span>
              <input type="number" min="0" max="31" bind:value={currentConfig.fallbackPresetIndex} />
            </label>
          </div>

          <!-- Power & Watchdog Protection -->
          <div class="settings-card">
            <h3>Power & Safety</h3>
            <label class="field">
              <span>Max Power (mW)</span>
              <input type="number" min="1000" max="100000" step="500" bind:value={currentConfig.maxPowerMw} />
            </label>
            <label class="field">
              <span>Watchdog Timeout (s)</span>
              <input type="number" min="1" max="300" bind:value={currentConfig.watchdogTimeoutS} />
            </label>
            <label class="field">
              <span>Gamma (x10)</span>
              <input type="number" min="10" max="30" bind:value={currentConfig.gammaX10} />
            </label>
            <label class="toggle-field">
              <input type="checkbox" bind:checked={currentConfig.softStartEnabled} />
              <span>Soft-Start Ramp Enabled ({currentConfig.softStartMs}ms)</span>
            </label>
          </div>

          <!-- DMX & Streaming Protocols -->
          <div class="settings-card">
            <h3>DMX & Network Streaming</h3>
            <label class="toggle-field">
              <input type="checkbox" bind:checked={currentConfig.protocol.sacnEnabled} />
              <span>sACN (E1.31) Protocol Enabled</span>
            </label>
            <label class="toggle-field">
              <input type="checkbox" bind:checked={currentConfig.protocol.artnetEnabled} />
              <span>Art-Net Protocol Enabled</span>
            </label>
            <label class="field">
              <span>Start Universe</span>
              <input type="number" min="1" max="63999" bind:value={currentConfig.protocol.universeStart} />
            </label>
            <label class="field">
              <span>Live Stream Target Layer</span>
              <input type="number" min="0" max="2" bind:value={currentConfig.protocol.liveStreamLayer} />
            </label>
          </div>

          <!-- Multi-Node Sync & Telemetry -->
          <div class="settings-card">
            <h3>Synchronization</h3>
            <label class="toggle-field">
              <input type="checkbox" bind:checked={currentConfig.syncEnabled} />
              <span>LumiGrid Node Sync Enabled</span>
            </label>
            <label class="field">
              <span>Sync Role</span>
              <select bind:value={currentConfig.syncRole}>
                <option value={0}>Automatic (Elected)</option>
                <option value={1}>Leader (Master)</option>
                <option value={2}>Follower (Slave)</option>
              </select>
            </label>
            {#if currentConfig.syncEnabled}
              <label class="field">
                <span>Sync Token (Domain ID)</span>
                <input type="number" min="0" max="4294967295" bind:value={currentConfig.syncToken} />
              </label>
            {/if}
            <label class="toggle-field">
              <input type="checkbox" bind:checked={currentConfig.mqttEnabled} />
              <span>MQTT Telemetry Enabled</span>
            </label>
            {#if currentConfig.mqttEnabled}
              <label class="field">
                <span>Broker Host</span>
                <input type="text" bind:value={currentConfig.mqttBroker} />
              </label>
              <label class="field">
                <span>Broker Port</span>
                <input type="number" min="1" max="65535" bind:value={currentConfig.mqttPort} />
              </label>
            {/if}
          </div>
        </div>
      </div>
    {/if}

  <!-- TAB 2: BACKUP & RECOVERY -->
  {:else if activeTab === 'recovery'}
    <div class="recovery-container">
      <div class="recovery-grid">
        <!-- Export Backup -->
        <div class="recovery-card">
          <div class="rec-head">
            <span class="rec-icon">📦</span>
            <div>
              <h3>Export Full Node Backup</h3>
              <p>Download a complete JSON snapshot containing all 32 scenes, timeline data, schedules, and device configuration.</p>
            </div>
          </div>
          <button
            class="btn-primary"
            onclick={downloadNodeBackup}
            disabled={backupLoading || !selectedNodeIp}
            type="button"
          >
            {backupLoading ? 'Exporting...' : 'Export Backup JSON'}
          </button>
        </div>

        <!-- Restore Backup -->
        <div class="recovery-card">
          <div class="rec-head">
            <span class="rec-icon">♻️</span>
            <div>
              <h3>Restore from Backup</h3>
              <p>Upload a previously exported LumiGrid JSON backup to replace configuration and restore scenes.</p>
            </div>
          </div>

          <div class="restore-drop">
            <input
              type="file"
              accept=".json"
              id="restore-file-input"
              onchange={onRestoreFileSelected}
              class="file-input-hidden"
            />
            <label for="restore-file-input" class="restore-file-label">
              {restoreFile ? `Selected: ${restoreFile.name}` : 'Choose Backup .json File'}
            </label>
          </div>

          {#if restoreFile}
            <button
              class="btn-danger"
              onclick={() => (restoreConfirmOpen = true)}
              disabled={restoreLoading}
              type="button"
            >
              Confirm & Restore Backup
            </button>
          {/if}
        </div>
      </div>

    </div>

  <!-- TAB 3: OTA FIRMWARE UPDATE -->
  {:else if activeTab === 'ota'}
    <div class="ota-layout">
      <!-- Binary Upload Box -->
      <div class="ota-upload-card">
        <h3>Firmware Binary</h3>
        <div
          class="drop-zone"
          class:has-file={!!binFile}
          role="button"
          tabindex="0"
          ondrop={onDrop}
          ondragover={e => e.preventDefault()}
          onclick={() => document.getElementById('bin-input')?.click()}
          onkeydown={e => e.key === 'Enter' && document.getElementById('bin-input')?.click()}
        >
          <div class="drop-icon">{binFile ? '✓' : '⟳'}</div>
          <p class="drop-text">{binName || 'Drop .bin file here or click'}</p>
          {#if binFile}
            <p class="file-size">{(binFile.size / 1024).toFixed(0)} KB</p>
          {/if}
        </div>
        <input
          type="file"
          accept=".bin"
          id="bin-input"
          class="file-input-hidden"
          onchange={onFile}
        />

        {#if binFile}
          <button
            class="btn-primary full-w"
            onclick={startOta}
            disabled={runningOta || !selected.size}
            type="button"
          >
            {runningOta ? 'Flashing Nodes...' : `Flash ${selected.size} Selected Node${selected.size !== 1 ? 's' : ''}`}
          </button>
        {/if}

        <div class="warning-box">
          ⚠️ Ensure hardware remains on continuous power during OTA flashing. Interrupted flashes can cause bootloader recovery fallback.
        </div>
      </div>

      <!-- Target Node Fleet -->
      <div class="ota-nodes-card">
        <div class="nodes-header">
          <h3>Fleet Deployment Targets</h3>
          <div class="sel-all-row">
            <button class="btn-sm" onclick={() => (selected = new Set(nodeList))} type="button">Select All</button>
            <button class="btn-sm" onclick={() => (selected = new Set())} type="button">Select None</button>
            <button class="btn-sm" onclick={refreshVersions} disabled={runningOta} type="button">Refresh</button>
          </div>
        </div>

        {#each nodes as node}
          <div class="node-row" class:selected={selected.has(node.ip)}>
            <input
              type="checkbox"
              checked={selected.has(node.ip)}
              onchange={() => toggleNode(node.ip)}
              id={`n_${node.ip}`}
              disabled={runningOta}
              class="node-check"
            />
            <label for={`n_${node.ip}`} class="node-label">
              <span class="node-name">{node.name}</span>
              <span class="node-ip">{node.ip}</span>
            </label>
            <span class="node-ver">v{node.version}</span>
            <span class="node-status" style:color={STATUS_COLOR[node.status]}>
              {STATUS_LABEL[node.status]}
            </span>

            {#if node.status === 'uploading' || node.status === 'rollback'}
              <div class="prog-wrap">
                <div
                  class="prog-bar"
                  style="width: {node.progress}%; background: {node.status === 'rollback' ? '#ff9500' : '#00d4ff'};"
                ></div>
              </div>
            {/if}

            {#if node.error}
              <div class="node-err">{node.error}</div>
            {/if}

            {#if node.status === 'failed' && prevBin}
              <button class="btn-sm rollback-btn" onclick={() => rollback(node)} type="button">
                Rollback
              </button>
            {/if}
          </div>
        {/each}

        {#if !nodes.length}
          <div class="empty-box">No fleet nodes registered. Add a node from Fleet.</div>
        {/if}
      </div>
    </div>
  {/if}
</div>

<OperatorDialog
  open={restoreConfirmOpen}
  title="Restore backup to this node?"
  description={restoreFile
    ? `“${restoreFile.name}” will replace the current configuration and scenes on ${selectedNodeIp}. Unsaved changes on the node will be overwritten.`
    : ''}
  confirmLabel="Restore Backup"
  tone="danger"
  busy={restoreLoading}
  onconfirm={executeRestore}
  oncancel={() => (restoreConfirmOpen = false)}
/>

<OperatorDialog
  open={otaConfirmOpen}
  title="Start staged firmware deployment?"
  description={binFile
    ? `Deploy “${binName}” to ${selected.size} selected node${selected.size === 1 ? '' : 's'}. LumiGrid will run preflight, update one canary, then roll out to the remaining fleet two nodes at a time.`
    : ''}
  confirmLabel="Start Deployment"
  tone="warning"
  busy={runningOta}
  onconfirm={executeOta}
  oncancel={() => (otaConfirmOpen = false)}
/>

<style>
  .config-page {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    color: #f5f5f7;
    max-width: 1180px;
    margin: 0 auto;
    width: 100%;
  }

  .page-header {
    position: sticky;
    top: 0;
    z-index: 18;
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
    background: rgba(22,22,24,.90);
    backdrop-filter: blur(22px);
    -webkit-backdrop-filter: blur(22px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
    padding: 0.85rem 1.25rem;
  }
  .page-header h1 {
    margin: 0;
    font-size: 1.25rem;
    font-weight: 700;
  }
  .sub {
    margin: 0.25rem 0 0;
    font-size: 0.8rem;
    color: #86868b;
  }
  .ip-tag {
    color: var(--dash-led);
    font-family: monospace;
  }

  /* Tab Bar */
  .tab-bar {
    display: flex;
    gap: 0.4rem;
  }
  .tab {
    background: transparent;
    border: none;
    padding: 0.45rem 0.85rem;
    color: #86868b;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    border-radius: 8px;
    transition: all 0.15s ease;
  }
  .tab:hover {
    color: #f5f5f7;
    background: rgba(255, 255, 255, 0.04);
  }
  .tab.active {
    color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.12);
  }

  .error-banner {
    background: rgba(255, 69, 58, 0.18);
    border: 1px solid rgba(255, 69, 58, 0.35);
    color: #ff453a;
    padding: 0.65rem 1rem;
    border-radius: 8px;
    font-size: 0.85rem;
  }
  .toast-banner {
    background: rgba(48, 209, 88, 0.18);
    border: 1px solid rgba(48, 209, 88, 0.35);
    color: #30d158;
    padding: 0.65rem 1rem;
    border-radius: 8px;
    font-size: 0.85rem;
  }

  .loading-box, .empty-box {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 1rem;
    min-height: 250px;
    color: #86868b;
  }
  .spinner {
    width: 30px;
    height: 30px;
    border: 3px solid rgba(255, 255, 255, 0.1);
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin { to { transform: rotate(360deg); } }

  /* Config Container */
  .config-container {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }
  .config-toolbar {
    position: sticky;
    top: 74px;
    z-index: 16;
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 12px;
    padding: 0.75rem 1.25rem;
    flex-wrap: wrap;
    gap: 1rem;
  }
  .status-group {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .status-title {
    font-size: 0.8rem;
    color: #86868b;
    font-weight: 600;
  }
  .badge {
    font-size: 0.7rem;
    font-weight: 700;
    padding: 0.2rem 0.55rem;
    border-radius: 6px;
    letter-spacing: 0.04em;
  }
  .badge-edited {
    background: rgba(255, 159, 10, 0.15);
    border: 1px solid rgba(255, 159, 10, 0.4);
    color: #ff9f0a;
  }
  .badge-saved {
    background: rgba(48, 209, 88, 0.12);
    border: 1px solid rgba(48, 209, 88, 0.3);
    color: var(--dash-ok);
  }
  .config-actions {
    display: flex;
    gap: 0.5rem;
  }

  .settings-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    gap: 1.25rem;
  }
  .settings-card {
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    box-shadow: var(--dash-shadow);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
  }
  .settings-card h3 {
    margin: 0 0 0.4rem;
    font-size: 0.95rem;
    font-weight: 600;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    padding-bottom: 0.4rem;
  }

  .field {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .field span {
    font-size: 0.72rem;
    color: #86868b;
    font-weight: 600;
    text-transform: uppercase;
  }
  .field input, .field select {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: #f5f5f7;
    font-size: 0.85rem;
    padding: 0.4rem 0.6rem;
  }
  .field input:focus-visible, .field select:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12);
  }
  .toggle-field {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-size: 0.8rem;
    color: #ddd;
    cursor: pointer;
  }
  .toggle-field input {
    accent-color: var(--dash-accent);
    width: 15px;
    height: 15px;
  }

  /* Recovery Grid */
  .recovery-container {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }
  .recovery-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(360px, 1fr));
    gap: 1.25rem;
  }
  .recovery-card {
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    box-shadow: var(--dash-shadow);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1.5rem;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }
  .rec-head {
    display: flex;
    gap: 1rem;
    align-items: flex-start;
  }
  .rec-icon {
    font-size: 2rem;
  }
  .rec-head h3 {
    margin: 0;
    font-size: 1rem;
    font-weight: 600;
  }
  .rec-head p {
    margin: 0.35rem 0 0;
    font-size: 0.8rem;
    color: #86868b;
    line-height: 1.4;
  }
  .restore-drop {
    display: flex;
  }
  .file-input-hidden {
    display: none;
  }
  .restore-file-label {
    display: block;
    width: 100%;
    padding: 0.8rem;
    border: 2px dashed rgba(255, 255, 255, 0.15);
    border-radius: 8px;
    text-align: center;
    cursor: pointer;
    font-size: 0.82rem;
    color: var(--dash-led);
    transition: all 0.15s ease;
  }
  .restore-file-label:hover {
    border-color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.08);
  }

  /* OTA Layout */
  .ota-layout {
    display: grid;
    grid-template-columns: 280px 1fr;
    gap: 1.25rem;
    align-items: flex-start;
  }
  .ota-upload-card, .ota-nodes-card {
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    box-shadow: var(--dash-shadow);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
  }
  .ota-upload-card h3, .ota-nodes-card h3 {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 600;
  }
  .drop-zone {
    border: 2px dashed rgba(255, 255, 255, 0.15);
    border-radius: 12px;
    padding: 1.5rem 1rem;
    text-align: center;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .drop-zone:hover, .drop-zone.has-file {
    border-color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.06);
  }
  .drop-icon {
    font-size: 1.8rem;
    margin-bottom: 0.4rem;
    color: #86868b;
  }
  .drop-zone.has-file .drop-icon {
    color: #30d158;
  }
  .drop-text {
    margin: 0;
    font-size: 0.82rem;
    color: #f5f5f7;
  }
  .file-size {
    margin: 0.25rem 0 0;
    font-size: 0.72rem;
    color: #86868b;
    font-family: monospace;
  }
  .warning-box {
    background: rgba(255, 159, 10, 0.1);
    border: 1px solid rgba(255, 159, 10, 0.3);
    border-radius: 8px;
    color: #ff9f0a;
    font-size: 0.75rem;
    padding: 0.6rem 0.8rem;
    line-height: 1.4;
  }

  .nodes-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: 0.5rem;
  }
  .sel-all-row {
    display: flex;
    gap: 0.35rem;
  }
  .node-row {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.5rem 0.75rem;
    padding: 0.6rem 0.5rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.04);
    transition: background .15s ease;
  }
  .node-row:hover { background: rgba(255,255,255,.025); }
  .node-row:last-child {
    border-bottom: none;
  }
  .node-check {
    accent-color: var(--dash-accent);
    width: 16px;
    height: 16px;
  }
  .node-label {
    display: flex;
    flex-direction: column;
    flex: 1;
    min-width: 100px;
    cursor: pointer;
  }
  .node-name {
    font-size: 0.85rem;
    font-weight: 600;
  }
  .node-ip {
    font-size: 0.72rem;
    color: #86868b;
    font-family: monospace;
  }
  .node-ver {
    font-size: 0.75rem;
    color: #86868b;
    font-family: monospace;
    margin-left: auto;
  }
  .node-status {
    font-size: 0.78rem;
    font-weight: 600;
  }
  .prog-wrap {
    width: 100%;
    height: 4px;
    background: rgba(255, 255, 255, 0.08);
    border-radius: 999px;
    overflow: hidden;
  }
  .prog-bar {
    height: 100%;
    border-radius: 999px;
    transition: width 0.2s ease;
  }
  .node-err {
    width: 100%;
    font-size: 0.72rem;
    color: #ff453a;
    font-family: monospace;
  }

  /* Buttons */
  .btn-primary {
    background: var(--dash-accent);
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 0.45rem 1rem;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .btn-primary:hover:not(:disabled) {
    background: var(--dash-accent-hover);
  }
  .btn-primary:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .btn-secondary {
    background: rgba(255, 255, 255, 0.06);
    color: #a1a1a6;
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 8px;
    padding: 0.45rem 0.85rem;
    font-size: 0.82rem;
    cursor: pointer;
  }
  .btn-secondary:disabled {
    opacity: 0.35;
    cursor: not-allowed;
  }
  .btn-danger {
    background: rgba(255, 69, 58, 0.2);
    color: #ff453a;
    border: 1px solid rgba(255, 69, 58, 0.4);
    border-radius: 8px;
    padding: 0.45rem 1rem;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
  }
  .btn-danger:hover:not(:disabled) {
    background: rgba(255, 69, 58, 0.3);
  }
  .btn-sm {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 6px;
    color: #f5f5f7;
    font-size: 0.72rem;
    padding: 0.25rem 0.5rem;
    cursor: pointer;
  }
  .full-w {
    width: 100%;
  }

  @media (max-width: 800px) {
    .ota-layout { grid-template-columns: 1fr; }
  }

  @media (max-width: 820px) {
    .page-header, .config-toolbar { position: static; }
    .tab-bar { overflow-x: auto; }
    .tab { white-space: nowrap; }
    .settings-grid, .recovery-grid, .ota-layout { grid-template-columns: 1fr; }
    .config-toolbar { align-items: stretch; }
    .config-actions { width: 100%; }
    .config-actions button { flex: 1; }
  }

</style>
