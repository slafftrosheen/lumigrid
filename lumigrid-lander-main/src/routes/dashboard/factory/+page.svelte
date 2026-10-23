<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import {
    getFactoryStatus,
    postFactoryEffect,
    postFactoryLane,
    postFactoryPwm,
    postFactoryAbort,
    postFactoryAutoLoop,
    startFactoryTestSession,
    confirmFactoryTestSession,
    abortFactoryTestSession,
    getFactoryValidationReport,
    startBurnInJob,
    stopBurnInJob,
    type FactoryStatus,
    type FactoryEffectName,
    type FactoryTestKind,
    type FactoryTestSession,
    type FactoryValidationReport,
  } from '$lib/api/node';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore, refreshHealth } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let status = $state<FactoryStatus | null>(null);
  let report = $state<FactoryValidationReport | null>(null);
  let loading = $state(true);
  let error = $state('');
  let flash = $state('');

  // Diagnostic Controls
  let brightness = $state(32);
  let durationMs = $state(5000);
  let aborting = $state(false);

  // Burn-In Controls (LG-035 §9: default 64, cap 96)
  let burnInMinutes = $state(60);
  let burnInBrightness = $state(64);
  let burnInSubmitting = $state(false);

  // Active operator confirmation prompt modal state
  let operatorPromptOpen = $state(false);
  let pendingSession = $state<FactoryTestSession | null>(null);
  let operatorNotes = $state('');

  // Manual Overrides
  const LANES = Array.from({ length: 8 }, (_, i) => i);
  const PWM_CHANNELS = Array.from({ length: 8 }, (_, i) => i);
  let laneColors = $state(Array.from({ length: 8 }, () => ({ r: 255, g: 0, b: 0 })));
  let pwmDuties = $state(Array.from({ length: 8 }, () => 0));
  let manualDeckOpen = $state(false);

  // Live telemetry derived from authoritative nodeStore snapshot and local status poll
  const liveNode = $derived($nodeStore.snapshot?.node);
  const liveRuntime = $derived($nodeStore.snapshot?.runtime);
  const liveOutput = $derived($nodeStore.snapshot?.output);
  const liveJob = $derived($nodeStore.snapshot?.job);

  const isTestActive = $derived(liveRuntime?.testActive ?? status?.testActive ?? false);
  const activeEffect = $derived(liveRuntime?.effect ?? status?.effect ?? 'None (Idle)');
  const isOutputsEnabled = $derived(liveOutput?.enabled ?? status?.outputsEnabled ?? false);
  const isAutoLoop = $derived(liveRuntime?.autoLoop ?? status?.autoLoop ?? false);
  const currentBrightness = $derived(status?.brightness ?? liveRuntime?.brightness ?? 0);
  const commitFailures = $derived(status?.commitFailures ?? liveOutput?.faultCount ?? 0);
  const commandRejections = $derived(status?.commandRejections ?? 0);
  const currentSession = $derived(status?.session ?? null);

  const ALL_EFFECTS: { name: FactoryEffectName; label: string; desc: string; icon: string; category: 'quick' | 'burnin' | 'pwm' }[] = [
    { name: 'identity',       label: 'Identity',       desc: 'Flashing node identification pulse', icon: '◻', category: 'quick' },
    { name: 'pixel_chase',    label: 'Pixel Chase',    desc: 'Sequential lane walker',             icon: '→', category: 'quick' },
    { name: 'rainbow',        label: 'Rainbow',        desc: 'Continuous spectrum sweep',         icon: '◐', category: 'quick' },
    { name: 'white_walk',     label: 'White Walk',     desc: 'Full spectrum lumen test',          icon: '◈', category: 'quick' },
    { name: 'combined_pulse', label: 'Combined Pulse', desc: 'Synchronized LED and PWM cycle',     icon: '◉', category: 'quick' },
    { name: 'blackout',       label: 'Blackout',       desc: 'Immediately blank all outputs',     icon: '⬛', category: 'quick' },
    { name: 'pwm_walk',       label: 'PWM Walk',       desc: 'Step through auxiliary channels',   icon: '▤', category: 'pwm' },
    { name: 'pwm_fade',       label: 'PWM Fade',       desc: 'Smooth triangle modulation',        icon: '◑', category: 'pwm' },
    { name: 'stress_cycle',   label: 'Stress Cycle',   desc: '60-second diagnostic stress cycle', icon: '⬥', category: 'burnin' },
  ];

  let pollInterval: ReturnType<typeof setInterval> | null = null;
  let refreshGen = 0;

  async function refresh(targetIp = nodeIp, gen = refreshGen) {
    if (!targetIp) {
      status = null;
      report = null;
      loading = false;
      return;
    }
    try {
      const [s, r] = await Promise.allSettled([
        getFactoryStatus(targetIp),
        getFactoryValidationReport(targetIp),
      ]);
      if (gen !== refreshGen) return;

      if (s.status === 'fulfilled') {
        status = s.value;
        // Check if session transitioned to waiting_for_operator
        if (status.session && status.session.state === 'waiting_for_operator') {
          if (!operatorPromptOpen || pendingSession?.id !== status.session.id) {
            pendingSession = status.session;
            operatorPromptOpen = true;
          }
        }
      }
      if (r.status === 'fulfilled') {
        report = r.value;
      }
      error = '';
    } catch (e) {
      if (gen !== refreshGen) return;
      error = String(e);
    } finally {
      if (gen === refreshGen) {
        loading = false;
      }
    }
  }

  async function runChannelValidation(kind: FactoryTestKind, channel: number) {
    if (!nodeIp) return;
    try {
      error = '';
      const res = await startFactoryTestSession(nodeIp, kind, channel, durationMs, brightness);
      flash = `Started validation session #${res.sessionId} on ${kind} ${channel >= 0 ? channel : ''}`;
      setTimeout(() => (flash = ''), 3000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function submitOperatorConfirmation(passed: boolean) {
    if (!nodeIp || !pendingSession) return;
    try {
      error = '';
      await confirmFactoryTestSession(nodeIp, pendingSession.id, passed, operatorNotes || undefined);
      flash = `Session #${pendingSession.id} operator confirmation recorded: ${passed ? 'PASSED' : 'FAILED'}`;
      operatorPromptOpen = false;
      pendingSession = null;
      operatorNotes = '';
      setTimeout(() => (flash = ''), 3000);
      await refresh();
      refreshHealth(nodeIp).catch(() => {});
    } catch (e) {
      error = String(e);
    }
  }

  async function runCombinedTest() {
    if (!nodeIp) return;
    try {
      error = '';
      const res = await startFactoryTestSession(nodeIp, 'combined_outputs', 0xFF, 8000, Math.min(brightness, 48));
      flash = `Combined output test started (Session #${res.sessionId})`;
      setTimeout(() => (flash = ''), 3000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function runSafetyGateTest() {
    if (!nodeIp) return;
    try {
      error = '';
      const res = await startFactoryTestSession(nodeIp, 'safety_gate', 0xFF, 5000, 32);
      flash = `Safety gate verification started (Session #${res.sessionId})`;
      setTimeout(() => (flash = ''), 3000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function triggerBurnInJob() {
    if (!nodeIp || burnInSubmitting) return;
    burnInSubmitting = true;
    try {
      error = '';
      // Enforce controlled load limits (capped at 96)
      const clampedBri = Math.min(Math.max(burnInBrightness, 1), 96);
      const res = await startBurnInJob(nodeIp, burnInMinutes, clampedBri);
      flash = `Burn-in Job #${res.jobId} started (${burnInMinutes}m @ ${clampedBri}/255 load)`;
      setTimeout(() => (flash = ''), 4000);
      await refresh();
    } catch (e) {
      error = String(e);
    } finally {
      burnInSubmitting = false;
    }
  }

  async function abortBurnInJob() {
    if (!nodeIp) return;
    try {
      error = '';
      await stopBurnInJob(nodeIp);
      flash = 'Burn-in cancellation requested. Waiting for worker status.';
      setTimeout(() => (flash = ''), 3000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function sendAbort() {
    if (!nodeIp || aborting) return;
    aborting = true;
    try {
      await abortFactoryTestSession(nodeIp);
      flash = 'Factory cancellation requested. Waiting for worker status.';
      operatorPromptOpen = false;
      pendingSession = null;
      setTimeout(() => (flash = ''), 3000);
      await refresh();
      refreshHealth(nodeIp).catch(() => {});
    } catch (e) {
      error = String(e);
    } finally {
      aborting = false;
    }
  }

  async function sendEffect(name: FactoryEffectName) {
    if (!nodeIp) return;
    try {
      await postFactoryEffect(nodeIp, name, brightness, durationMs);
      flash = `Launched test effect: ${name}`;
      setTimeout(() => (flash = ''), 2500);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function toggleAutoLoop() {
    if (!nodeIp || !status) return;
    try {
      await postFactoryAutoLoop(nodeIp, !status.autoLoop);
      await refresh();
      flash = `Auto-loop ${!status.autoLoop ? 'enabled' : 'disabled'}`;
      setTimeout(() => (flash = ''), 2000);
    } catch (e) {
      error = String(e);
    }
  }

  async function sendLane(lane: number) {
    if (!nodeIp) return;
    const c = laneColors[lane];
    try {
      await postFactoryLane(nodeIp, lane, c.r, c.g, c.b, brightness, durationMs);
      flash = `Lane A${lane + 1} pattern sent (RGB ${c.r},${c.g},${c.b})`;
      setTimeout(() => (flash = ''), 2000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function sendPwm(channel: number) {
    if (!nodeIp) return;
    try {
      await postFactoryPwm(nodeIp, channel, pwmDuties[channel], durationMs);
      flash = `PWM channel P${channel + 1} duty set to ${pwmDuties[channel]}`;
      setTimeout(() => (flash = ''), 2000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  function downloadReportJson() {
    if (!report) return;
    const payload = {
      node: liveNode,
      timestamp: new Date().toISOString(),
      report,
    };
    const dataStr = 'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(payload, null, 2));
    const dl = document.createElement('a');
    dl.setAttribute('href', dataStr);
    dl.setAttribute('download', `factory_validation_${liveNode?.id || 'node'}_${Date.now()}.json`);
    document.body.appendChild(dl);
    dl.click();
    dl.remove();
  }

  function copyReportJson() {
    if (!report) return;
    const payload = {
      node: liveNode,
      timestamp: new Date().toISOString(),
      report,
    };
    navigator.clipboard.writeText(JSON.stringify(payload, null, 2));
    flash = 'Report JSON copied to clipboard!';
    setTimeout(() => (flash = ''), 2500);
  }

  const overallVerdict = $derived.by(() => {
    if (!report) return 'UNTESTED';
    const allAddr = report.addressable.length === 8 && report.addressable.every(a => a.tested);
    const passAddr = report.addressable.every(a => a.passed);
    const allPwm = report.pwm.length === 8 && report.pwm.every(p => p.tested);
    const passPwm = report.pwm.every(p => p.passed);

    if (!allAddr || !allPwm) return 'INCOMPLETE';
    if (!passAddr || !passPwm || (report.combined.tested && !report.combined.passed) || (report.safetyGate.tested && !report.safetyGate.passed)) {
      return 'FAILED';
    }
    return 'PASSED (Operator Certified)';
  });

  onMount(() => {
    nodeIp = getSelectedNodeIp() || '';
    refresh();
    pollInterval = setInterval(() => {
      refreshGen++;
      refresh(nodeIp, refreshGen);
    }, 1500);
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });
</script>

<div class="factory-container">
  <!-- Top Navigation & Live Telemetry Strip -->
  <header class="telemetry-bar">
    <div class="telemetry-item">
      <span class="telemetry-label">TARGET NODE</span>
      <span class="telemetry-value mono">{nodeIp || 'No node selected'}</span>
    </div>

    <div class="telemetry-divider"></div>

    <div class="telemetry-item">
      <span class="telemetry-label">FACTORY STATE</span>
      <span class="status-pill status-{status?.state || (isTestActive ? 'running' : 'idle')}">
        <span class="dot"></span>
        {#if status?.session?.stage}
          {status.session.stage.toUpperCase()} ({(status.session.stageIndex ?? 0) + 1}/{status.session.stageCount ?? 6})
        {:else}
          {status?.state?.toUpperCase() || (isTestActive ? 'RUNNING' : 'IDLE')}
        {/if}
      </span>
    </div>

    <div class="telemetry-divider"></div>

    <div class="telemetry-item">
      <span class="telemetry-label">ACTIVE EFFECT</span>
      <span class="telemetry-value">{activeEffect}</span>
    </div>

    <div class="telemetry-divider"></div>

    <div class="telemetry-item">
      <span class="telemetry-label">SAFETY GATE</span>
      <span class="telemetry-value {isOutputsEnabled ? 'text-ok' : 'text-danger'}">
        {isOutputsEnabled ? 'OUTPUTS ARMED' : 'FAIL-CLOSED'}
      </span>
    </div>

    <div class="telemetry-divider"></div>

    <div class="telemetry-item">
      <span class="telemetry-label">COMMIT FAILURES</span>
      <span class="telemetry-value mono {commitFailures > 0 ? 'text-danger' : 'text-muted'}">
        {commitFailures}
      </span>
    </div>

    <div class="telemetry-spacer"></div>

    <button
      class="btn-danger-abort"
      onclick={sendAbort}
      disabled={aborting}
      title="Immediately blackout and terminate all test sessions"
    >
      <span class="abort-icon">⬛</span>
      EMERGENCY BLACKOUT
    </button>
  </header>

  {#if flash}
    <div class="flash-banner">{flash}</div>
  {/if}

  {#if error}
    <div class="error-banner">
      <strong>Communication Error:</strong> {error}
    </div>
  {/if}

  <!-- Section 1: Board Identity Card -->
  <section class="section-card board-identity-card">
    <div class="card-header">
      <div class="header-left">
        <span class="step-badge">STEP 1</span>
        <h2>Board Identity & Physical Hardware</h2>
      </div>
      <button class="btn-secondary btn-sm" onclick={() => refresh()}>⟳ Refresh Telemetry</button>
    </div>

    <div class="identity-grid">
      <div class="identity-item">
        <span class="prop-label">Device Name / ID</span>
        <span class="prop-value font-bold">{liveNode?.id || 'Lumigrid-Node'}</span>
      </div>
      <div class="identity-item">
        <span class="prop-label">Hardware Architecture</span>
        <span class="prop-value">ESP32 (8x Addressable + 8x PWM)</span>
      </div>
      <div class="identity-item">
        <span class="prop-label">MAC Address</span>
        <span class="prop-value mono">{liveNode?.id || 'Unknown'}</span>
      </div>
      <div class="identity-item">
        <span class="prop-label">Firmware Status</span>
        <span class="prop-value text-ok">Production Factory Build</span>
      </div>
      <div class="identity-item">
        <span class="prop-label">Control Owner</span>
        <span class="prop-value mono">{liveRuntime?.controlOwner || (status?.testActive ? 'Factory' : 'Normal')}</span>
      </div>
      <div class="identity-item">
        <span class="prop-label">Command Rejections</span>
        <span class="prop-value mono">{commandRejections}</span>
      </div>
    </div>
  </section>

  <!-- Section 2: Addressable Channels Validation (A1 – A8) -->
  <section class="section-card">
    <div class="card-header">
      <div class="header-left">
        <span class="step-badge">STEP 2</span>
        <h2>Addressable LED Channels (A1 – A8)</h2>
      </div>
      <span class="header-subtitle">Direct RMT pixel validation with RGBW cycle verification</span>
    </div>

    <p class="section-help">
      Software confirms transmission timing. Operator visual inspection certifies physical LEDs have zero dead pixels, correct color parity, and no ground bounce.
    </p>

    <div class="channels-grid">
      {#each LANES as lane}
        {@const chStatus = report?.addressable[lane]}
        {@const isCurrentLane = currentSession?.channel === lane && isTestActive}
        <div class="channel-card {isCurrentLane ? 'channel-active' : ''} {chStatus?.passed ? 'card-passed' : chStatus?.tested ? 'card-failed' : ''}">
          <div class="channel-top">
            <span class="channel-title">Channel A{lane + 1}</span>
            {#if chStatus?.passed}
              <span class="badge badge-success">✓ PASSED</span>
            {:else if chStatus?.tested}
              <span class="badge badge-danger">✗ FAILED</span>
            {:else if isCurrentLane}
              {#if currentSession?.stage}
                <span class="badge badge-warning">{currentSession.stage.toUpperCase()} ({(currentSession.stageIndex ?? 0) + 1}/{currentSession.stageCount ?? 6})</span>
              {:else}
                <span class="badge badge-warning">TESTING...</span>
              {/if}
            {:else}
              <span class="badge badge-idle">UNTESTED</span>
            {/if}
          </div>

          <div class="channel-body">
            <span class="detail-label">Hardware Port: GPIO Lane {lane}</span>
            <span class="detail-label">Pattern: 5-Step RGBW Walk</span>
          </div>

          <div class="channel-actions">
            <button
              class="btn-primary btn-sm full-w"
              onclick={() => runChannelValidation('addressable_channel', lane)}
              disabled={isTestActive}
            >
              Test A{lane + 1} (5s)
            </button>
          </div>
        </div>
      {/each}
    </div>
  </section>

  <!-- Section 3: PWM Channels Validation (P1 – P8) -->
  <section class="section-card">
    <div class="card-header">
      <div class="header-left">
        <span class="step-badge">STEP 3</span>
        <h2>PWM Channels Validation (P1 – P8)</h2>
      </div>
      <span class="header-subtitle">0% → 25% → 50% → 100% duty cycle step verification</span>
    </div>

    <p class="section-help">
      Verifies low-side MOSFET drivers and auxiliary lighting drivers across channels 8 through 15 without overloading the 12V/24V power rail.
    </p>

    <div class="channels-grid">
      {#each PWM_CHANNELS as ch}
        {@const chStatus = report?.pwm[ch]}
        {@const isCurrentPwm = currentSession?.channel === (ch + 8) && isTestActive}
        <div class="channel-card {isCurrentPwm ? 'channel-active' : ''} {chStatus?.passed ? 'card-passed' : chStatus?.tested ? 'card-failed' : ''}">
          <div class="channel-top">
            <span class="channel-title">Channel P{ch + 1}</span>
            {#if chStatus?.passed}
              <span class="badge badge-success">✓ PASSED</span>
            {:else if chStatus?.tested}
              <span class="badge badge-danger">✗ FAILED</span>
            {:else if isCurrentPwm}
              {#if currentSession?.stage}
                <span class="badge badge-warning">{currentSession.stage.toUpperCase()} ({(currentSession.stageIndex ?? 0) + 1}/{currentSession.stageCount ?? 6})</span>
              {:else}
                <span class="badge badge-warning">TESTING...</span>
              {/if}
            {:else}
              <span class="badge badge-idle">UNTESTED</span>
            {/if}
          </div>

          <div class="channel-body">
            <span class="detail-label">Hardware Port: PWM #{ch + 8}</span>
            <span class="detail-label">Pattern: 4-Step Duty Ramp</span>
          </div>

          <div class="channel-actions">
            <button
              class="btn-primary btn-sm full-w"
              onclick={() => runChannelValidation('pwm_channel', ch + 8)}
              disabled={isTestActive}
            >
              Test P{ch + 1} (5s)
            </button>
          </div>
        </div>
      {/each}
    </div>
  </section>

  <!-- Section 4: System Integration & Safety Gates -->
  <div class="two-col-grid">
    <!-- Combined Outputs Test -->
    <section class="section-card">
      <div class="card-header">
        <div class="header-left">
          <span class="step-badge">STEP 4</span>
          <h2>Combined Output Test</h2>
        </div>
        {#if report?.combined.tested}
          <span class="badge {report.combined.passed ? 'badge-success' : 'badge-danger'}">
            {report.combined.passed ? '✓ PASSED' : '✗ FAILED'}
          </span>
        {/if}
      </div>

      <p class="section-help">
        Exercises all 8 addressable lanes and 8 PWM auxiliary channels synchronously to verify PSU stability, voltage sag tolerance, and bus arbitration under full load.
      </p>

      <div class="test-action-box">
        <button
          class="btn-primary full-w"
          onclick={runCombinedTest}
          disabled={isTestActive}
        >
          Run Combined Output Pulse (8s @ 48/255)
        </button>
      </div>
    </section>

    <!-- Safety Gate & Fail-Closed E-Stop Verification -->
    <section class="section-card">
      <div class="card-header">
        <div class="header-left">
          <span class="step-badge">STEP 5</span>
          <h2>E-Stop & Safety Gate</h2>
        </div>
        {#if report?.safetyGate.tested}
          <span class="badge {report.safetyGate.passed ? 'badge-success' : 'badge-danger'}">
            {report.safetyGate.passed ? '✓ PASSED' : '✗ FAILED'}
          </span>
        {/if}
      </div>

      <p class="section-help">
        Exercises the final hardware safety gate (<code>OutputController::try_commit()</code>). Verifies that when outputs are inhibited, all drivers strictly fail closed.
      </p>

      <div class="test-action-box">
        <button
          class="btn-warning full-w"
          onclick={runSafetyGateTest}
          disabled={isTestActive}
        >
          Verify Safety Gate Fail-Closed
        </button>
      </div>
    </section>
  </div>

  <!-- Section 5: Endurance & Burn-In Workflow (Proper Job) -->
  <section class="section-card burn-in-card">
    <div class="card-header">
      <div class="header-left">
        <span class="step-badge">STEP 6</span>
        <h2>Endurance & Thermal Burn-In Workflow</h2>
      </div>
      {#if report?.burnIn.tested}
        <span class="badge {report.burnIn.passed ? 'badge-success' : 'badge-danger'}">
          {report.burnIn.passed ? `✓ BURN-IN PASSED (${report.burnIn.durationS}s)` : '✗ BURN-IN FAILED'}
        </span>
      {/if}
    </div>

    <div class="safety-limit-notice">
      <span class="shield-icon">🛡</span>
      <div>
        <strong>Controlled Bench Power Limits Enforced:</strong>
        Burn-in is managed as a high-reliability background Job. Output brightness is capped at <strong>96 / 255</strong> (default <strong>64 / 255</strong>) to prevent uncontrolled bench thermal runaway.
      </div>
    </div>

    <div class="burn-in-controls">
      <div class="input-group">
        <label for="burn-in-duration">Duration (Minutes):</label>
        <select id="burn-in-duration" bind:value={burnInMinutes} disabled={isTestActive}>
          <option value={15}>15 Minutes (Quick Bench Validation)</option>
          <option value={30}>30 Minutes (Standard Bring-up)</option>
          <option value={60}>60 Minutes (Canonical Production Run)</option>
          <option value={120}>120 Minutes (Extended Thermal Soak)</option>
        </select>
      </div>

      <div class="input-group">
        <label for="burn-in-brightness">Load Brightness (Max 96):</label>
        <input
          id="burn-in-brightness"
          type="number"
          min="16"
          max="96"
          bind:value={burnInBrightness}
          disabled={isTestActive}
        />
      </div>

      <div class="burn-in-actions">
        {#if isTestActive && status?.session?.kind === 'burn_in'}
          <button class="btn-danger-abort" onclick={abortBurnInJob}>
            ■ Stop Burn-In Job
          </button>
        {:else}
          <button class="btn-primary" onclick={triggerBurnInJob} disabled={isTestActive || burnInSubmitting}>
            ▶ Start Controlled Burn-In Run
          </button>
        {/if}
      </div>
    </div>

    {#if isTestActive && status?.session?.kind === 'burn_in'}
      <div class="burn-in-progress-card">
        <div class="progress-info">
          <span>Running Burn-In Job... Elapsed: {Math.round((status?.elapsedMs || 0) / 1000)}s</span>
          <span>Remaining: {Math.round((status?.remainingMs || 0) / 1000)}s</span>
        </div>
        <div class="progress-bar-track">
          <div
            class="progress-bar-fill"
            style="width: {status?.durationMs ? Math.min(100, Math.round(((status?.elapsedMs || 0) / status.durationMs) * 100)) : 0}%"
          ></div>
        </div>
      </div>
    {/if}
  </section>

  <!-- Section 6: Validation Scorecard & Test Report -->
  <section class="section-card scorecard-card">
    <div class="card-header">
      <div class="header-left">
        <span class="step-badge">STEP 7</span>
        <h2>Validation Scorecard & Certification Report</h2>
      </div>
      <div class="header-right">
        <span class="verdict-pill verdict-{overallVerdict.startsWith('PASSED') ? 'pass' : overallVerdict === 'FAILED' ? 'fail' : 'pending'}">
          {overallVerdict}
        </span>
      </div>
    </div>

    <div class="scorecard-grid">
      <div class="score-card">
        <span class="score-num">{report?.addressable.filter(a => a.passed).length || 0} / 8</span>
        <span class="score-label">Addressable Lanes Passed</span>
      </div>

      <div class="score-card">
        <span class="score-num">{report?.pwm.filter(p => p.passed).length || 0} / 8</span>
        <span class="score-label">PWM Channels Passed</span>
      </div>

      <div class="score-card">
        <span class="score-num">{report?.combined.passed ? 'YES' : 'NO'}</span>
        <span class="score-label">Combined Pulse Test</span>
      </div>

      <div class="score-card">
        <span class="score-num">{report?.safetyGate.passed ? 'PASSED' : 'PENDING'}</span>
        <span class="score-label">Fail-Closed Safety Gate</span>
      </div>

      <div class="score-card">
        <span class="score-num">{report?.burnIn.passed ? `${report.burnIn.durationS}s` : '0s'}</span>
        <span class="score-label">Burn-In Duration (Errors: {report?.burnIn.errors || 0})</span>
      </div>
    </div>

    <div class="report-actions">
      <button class="btn-primary" onclick={downloadReportJson} disabled={!report}>
        ⬇ Download Certification JSON
      </button>
      <button class="btn-secondary" onclick={copyReportJson} disabled={!report}>
        📋 Copy Report JSON
      </button>
    </div>
  </section>

  <!-- Manual Diagnostics Accordion -->
  <section class="section-card accordion-card">
    <!-- svelte-ignore a11y_click_events_have_key_events -->
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <div class="accordion-header" onclick={() => (manualDeckOpen = !manualDeckOpen)}>
      <div class="accordion-title">
        <span>⚙ Ad-hoc Diagnostic Patterns & Manual Pin Overrides</span>
        <span class="badge badge-idle">{manualDeckOpen ? 'Hide' : 'Show'}</span>
      </div>
      <span class="chevron">{manualDeckOpen ? '▲' : '▼'}</span>
    </div>

    {#if manualDeckOpen}
      <div class="accordion-content">
        <div class="effects-row">
          {#each ALL_EFFECTS as eff}
            <button
              class="effect-chip"
              onclick={() => sendEffect(eff.name)}
              disabled={isTestActive}
              title={eff.desc}
            >
              <span class="eff-icon">{eff.icon}</span>
              <span class="eff-name">{eff.label}</span>
            </button>
          {/each}
          <button
            class="effect-chip chip-loop {isAutoLoop ? 'active' : ''}"
            onclick={toggleAutoLoop}
          >
            <span class="eff-icon">🔄</span>
            <span class="eff-name">Auto Loop {isAutoLoop ? 'ON' : 'OFF'}</span>
          </button>
        </div>

        <div class="manual-overrides-grid">
          <div class="override-col">
            <h4>Direct Addressable Lane Color Override</h4>
            <div class="lanes-override-list">
              {#each LANES as lane}
                <div class="override-row">
                  <span>A{lane + 1}</span>
                  <input type="number" min="0" max="255" bind:value={laneColors[lane].r} placeholder="R" class="rgb-input text-red" />
                  <input type="number" min="0" max="255" bind:value={laneColors[lane].g} placeholder="G" class="rgb-input text-green" />
                  <input type="number" min="0" max="255" bind:value={laneColors[lane].b} placeholder="B" class="rgb-input text-blue" />
                  <button class="btn-secondary btn-xs" onclick={() => sendLane(lane)} disabled={isTestActive}>Push</button>
                </div>
              {/each}
            </div>
          </div>

          <div class="override-col">
            <h4>Direct PWM Channel Duty Override</h4>
            <div class="lanes-override-list">
              {#each PWM_CHANNELS as ch}
                <div class="override-row">
                  <span>P{ch + 1}</span>
                  <input type="range" min="0" max="4095" bind:value={pwmDuties[ch]} class="pwm-slider" />
                  <span class="mono text-xs">{pwmDuties[ch]}</span>
                  <button class="btn-secondary btn-xs" onclick={() => sendPwm(ch)} disabled={isTestActive}>Push</button>
                </div>
              {/each}
            </div>
          </div>
        </div>
      </div>
    {/if}
  </section>
</div>

<!-- Operator Confirmation Modal (LG-035 §tyry4t) -->
{#if operatorPromptOpen && pendingSession}
  <div class="modal-backdrop">
    <div class="modal-dialog">
      <div class="modal-header">
        <span class="alert-icon">👁</span>
        <h3>Operator Physical Confirmation Required</h3>
      </div>

      <div class="modal-body">
        <p class="rule-reminder">
          <strong>Physical Hardware Certification Gate:</strong>
          Software confirmed commanded data stream completed. You must physically inspect the bench output before claiming the hardware passed.
        </p>

        <div class="session-summary-box">
          <div><strong>Session ID:</strong> #{pendingSession.id}</div>
          <div><strong>Validation Kind:</strong> {pendingSession.kind}</div>
          <div><strong>Target Hardware Channel:</strong> {pendingSession.channel >= 0 ? pendingSession.channel : 'All Channels'}</div>
        </div>

        <div class="operator-question">
          {#if pendingSession.kind === 'addressable_channel'}
            Did Channel A{pendingSession.channel + 1} cleanly cycle through Red → Green → Blue → White with zero flicker, zero stuck pixels, and correct color tint?
          {:else if pendingSession.kind === 'pwm_channel'}
            Did Channel P{(pendingSession.channel >= 8 ? pendingSession.channel - 8 : pendingSession.channel) + 1} smoothly ramp 0% → 25% → 50% → 100% duty with expected load current?
          {:else if pendingSession.kind === 'combined_outputs'}
            Did all 8 addressable strips and 8 PWM channels pulse synchronously without power supply voltage drops or controller resets?
          {:else if pendingSession.kind === 'safety_gate'}
            Did all outputs strictly remain off and fail-closed during the E-stop safety cycle?
          {:else}
            Did the burn-in cycle complete cleanly without thermal trips or anomalies?
          {/if}
        </div>

        <div class="notes-input-group">
          <label for="op-notes">Inspector Notes / Bench Observations (Optional):</label>
          <input id="op-notes" type="text" bind:value={operatorNotes} placeholder="e.g. Verified with lux meter / oscilloscope" />
        </div>
      </div>

      <div class="modal-footer">
        <button class="btn-danger" onclick={() => submitOperatorConfirmation(false)}>
          ✗ Mark FAILED
        </button>
        <button class="btn-success" onclick={() => submitOperatorConfirmation(true)}>
          ✓ Confirm Physical PASS
        </button>
      </div>
    </div>
  </div>
{/if}

<style>
  .factory-container {
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
    padding: 1.5rem;
    max-width: 1400px;
    margin: 0 auto;
    color: #e2e8f0;
  }

  .telemetry-bar {
    display: flex;
    align-items: center;
    gap: 1rem;
    padding: 0.85rem 1.25rem;
    background: rgba(15, 23, 42, 0.75);
    backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.35);
  }

  .telemetry-item {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
  }

  .telemetry-label {
    font-size: 0.65rem;
    font-weight: 700;
    letter-spacing: 0.06em;
    color: #94a3b8;
  }

  .telemetry-value {
    font-size: 0.85rem;
    font-weight: 600;
    color: #f8fafc;
  }

  .telemetry-divider {
    width: 1px;
    height: 28px;
    background: rgba(255, 255, 255, 0.1);
  }

  .telemetry-spacer {
    flex: 1;
  }

  .btn-danger-abort {
    display: inline-flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.6rem 1.25rem;
    background: #ef4444;
    color: #fff;
    font-weight: 700;
    font-size: 0.8rem;
    letter-spacing: 0.04em;
    border: none;
    border-radius: 8px;
    cursor: pointer;
    box-shadow: 0 0 16px rgba(239, 68, 68, 0.45);
    transition: all 0.2s ease;
  }

  .btn-danger-abort:hover:not(:disabled) {
    background: #dc2626;
    box-shadow: 0 0 24px rgba(239, 68, 68, 0.65);
    transform: translateY(-1px);
  }

  .btn-danger-abort:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .status-pill {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0.2rem 0.5rem;
    border-radius: 9999px;
    font-size: 0.75rem;
    font-weight: 700;
  }

  .status-pill .dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
  }

  .status-running {
    background: rgba(59, 130, 246, 0.2);
    color: #60a5fa;
    border: 1px solid rgba(59, 130, 246, 0.4);
  }
  .status-running .dot {
    background: #60a5fa;
    box-shadow: 0 0 8px #60a5fa;
  }

  .status-idle {
    background: rgba(148, 163, 184, 0.15);
    color: #cbd5e1;
    border: 1px solid rgba(148, 163, 184, 0.3);
  }
  .status-idle .dot {
    background: #94a3b8;
  }

  .status-waiting_for_operator {
    background: rgba(245, 158, 11, 0.2);
    color: #fbbf24;
    border: 1px solid rgba(245, 158, 11, 0.5);
    animation: pulse 1.5s infinite;
  }
  .status-waiting_for_operator .dot {
    background: #fbbf24;
    box-shadow: 0 0 8px #fbbf24;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.7; }
  }

  .section-card {
    background: rgba(15, 23, 42, 0.65);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    padding: 1.5rem;
    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.25);
  }

  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 0.75rem;
  }

  .header-left {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .step-badge {
    padding: 0.25rem 0.6rem;
    background: rgba(99, 102, 241, 0.25);
    border: 1px solid rgba(99, 102, 241, 0.4);
    color: #a5b4fc;
    font-size: 0.7rem;
    font-weight: 800;
    border-radius: 6px;
    letter-spacing: 0.05em;
  }

  h2 {
    font-size: 1.15rem;
    font-weight: 700;
    margin: 0;
    color: #f1f5f9;
  }

  .header-subtitle {
    font-size: 0.8rem;
    color: #94a3b8;
  }

  .section-help {
    font-size: 0.85rem;
    color: #94a3b8;
    margin: 0 0 1.25rem 0;
    line-height: 1.45;
  }

  .identity-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 1rem;
    background: rgba(0, 0, 0, 0.25);
    padding: 1rem;
    border-radius: 8px;
    border: 1px solid rgba(255, 255, 255, 0.04);
  }

  .identity-item {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .prop-label {
    font-size: 0.7rem;
    color: #64748b;
    text-transform: uppercase;
    font-weight: 600;
  }

  .prop-value {
    font-size: 0.85rem;
    color: #e2e8f0;
  }

  .channels-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
    gap: 0.85rem;
  }

  .channel-card {
    background: rgba(30, 41, 59, 0.4);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 8px;
    padding: 0.85rem;
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    transition: all 0.2s ease;
  }

  .channel-active {
    border-color: var(--dash-led);
    box-shadow: 0 0 14px rgba(255, 159, 10, 0.25);
  }

  .card-passed {
    border-color: rgba(34, 197, 94, 0.4);
    background: rgba(34, 197, 94, 0.05);
  }

  .card-failed {
    border-color: rgba(239, 68, 68, 0.4);
    background: rgba(239, 68, 68, 0.05);
  }

  .channel-top {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .channel-title {
    font-size: 0.9rem;
    font-weight: 700;
    color: #f8fafc;
  }

  .badge {
    font-size: 0.65rem;
    font-weight: 700;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
  }

  .badge-success { background: rgba(34, 197, 94, 0.2); color: #4ade80; border: 1px solid rgba(34, 197, 94, 0.4); }
  .badge-danger  { background: rgba(239, 68, 68, 0.2); color: #f87171; border: 1px solid rgba(239, 68, 68, 0.4); }
  .badge-warning { background: rgba(245, 158, 11, 0.2); color: #fbbf24; border: 1px solid rgba(245, 158, 11, 0.4); }
  .badge-idle    { background: rgba(148, 163, 184, 0.1); color: #94a3b8; }

  .channel-body {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
  }

  .detail-label {
    font-size: 0.65rem;
    color: #94a3b8;
  }

  .two-col-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
    gap: 1.5rem;
  }

  .safety-limit-notice {
    display: flex;
    align-items: center;
    gap: 0.85rem;
    padding: 0.75rem 1rem;
    background: rgba(245, 158, 11, 0.1);
    border: 1px solid rgba(245, 158, 11, 0.3);
    border-radius: 8px;
    font-size: 0.85rem;
    color: #fbbf24;
    margin-bottom: 1.25rem;
  }

  .burn-in-controls {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-end;
    gap: 1.25rem;
  }

  .input-group {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }

  .input-group label {
    font-size: 0.75rem;
    font-weight: 600;
    color: #94a3b8;
  }

  select, input[type="number"], input[type="text"] {
    background: rgba(30, 41, 59, 0.8);
    border: 1px solid rgba(255, 255, 255, 0.1);
    color: #f1f5f9;
    padding: 0.5rem 0.75rem;
    border-radius: 6px;
    font-size: 0.85rem;
  }

  .burn-in-progress-card {
    margin-top: 1.25rem;
    background: rgba(0, 0, 0, 0.3);
    padding: 0.85rem 1rem;
    border-radius: 8px;
    border: 1px solid rgba(255, 255, 255, 0.05);
  }

  .progress-info {
    display: flex;
    justify-content: space-between;
    font-size: 0.75rem;
    color: #cbd5e1;
    margin-bottom: 0.4rem;
  }

  .progress-bar-track {
    width: 100%;
    height: 8px;
    background: rgba(255, 255, 255, 0.08);
    border-radius: 4px;
    overflow: hidden;
  }

  .progress-bar-fill {
    height: 100%;
    background: linear-gradient(90deg, #f59e0b, #fbbf24);
    transition: width 0.3s ease;
  }

  .scorecard-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 1rem;
    margin-bottom: 1.5rem;
  }

  .score-card {
    background: rgba(0, 0, 0, 0.25);
    border: 1px solid rgba(255, 255, 255, 0.05);
    border-radius: 8px;
    padding: 1rem;
    text-align: center;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }

  .score-num {
    font-size: 1.25rem;
    font-weight: 800;
    color: #f8fafc;
  }

  .score-label {
    font-size: 0.7rem;
    color: #94a3b8;
    text-transform: uppercase;
    font-weight: 600;
  }

  .verdict-pill {
    padding: 0.3rem 0.85rem;
    font-size: 0.8rem;
    font-weight: 800;
    border-radius: 9999px;
  }

  .verdict-pass { background: rgba(34, 197, 94, 0.25); color: #4ade80; border: 1px solid #22c55e; }
  .verdict-fail { background: rgba(239, 68, 68, 0.25); color: #f87171; border: 1px solid #ef4444; }
  .verdict-pending { background: rgba(148, 163, 184, 0.15); color: #cbd5e1; border: 1px solid rgba(148, 163, 184, 0.3); }

  .report-actions {
    display: flex;
    gap: 1rem;
  }

  .accordion-card {
    padding: 0;
    overflow: hidden;
  }

  .accordion-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 1.25rem 1.5rem;
    cursor: pointer;
    background: rgba(30, 41, 59, 0.3);
  }

  .accordion-header:hover {
    background: rgba(30, 41, 59, 0.5);
  }

  .accordion-title {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    font-weight: 700;
    font-size: 0.95rem;
  }

  .accordion-content {
    padding: 1.5rem;
    border-top: 1px solid rgba(255, 255, 255, 0.05);
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }

  .effects-row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
  }

  .effect-chip {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0.4rem 0.8rem;
    background: rgba(30, 41, 59, 0.6);
    border: 1px solid rgba(255, 255, 255, 0.08);
    color: #e2e8f0;
    border-radius: 6px;
    font-size: 0.8rem;
    font-weight: 600;
    cursor: pointer;
  }

  .effect-chip:hover:not(:disabled) {
    background: rgba(255, 255, 255, 0.1);
    border-color: var(--dash-led);
  }

  .manual-overrides-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    gap: 1.5rem;
  }

  .override-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 0.4rem;
  }

  .rgb-input {
    width: 60px;
    padding: 0.25rem;
    text-align: center;
  }

  .pwm-slider {
    flex: 1;
  }

  /* Modal Backdrop & Dialog */
  .modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.75);
    backdrop-filter: blur(8px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
    padding: 1rem;
  }

  .modal-dialog {
    background: #0f172a;
    border: 2px solid #f59e0b;
    border-radius: 12px;
    width: 100%;
    max-width: 540px;
    box-shadow: 0 0 32px rgba(245, 158, 11, 0.35);
    overflow: hidden;
  }

  .modal-header {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 1.25rem;
    background: rgba(245, 158, 11, 0.15);
    border-bottom: 1px solid rgba(245, 158, 11, 0.3);
  }

  .modal-header h3 {
    margin: 0;
    color: #fbbf24;
    font-size: 1.1rem;
    font-weight: 700;
  }

  .modal-body {
    padding: 1.5rem;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }

  .rule-reminder {
    font-size: 0.8rem;
    color: #cbd5e1;
    background: rgba(0, 0, 0, 0.35);
    padding: 0.75rem;
    border-radius: 6px;
    border-left: 3px solid #f59e0b;
    margin: 0;
  }

  .session-summary-box {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    font-size: 0.8rem;
    color: #94a3b8;
  }

  .operator-question {
    font-size: 0.95rem;
    font-weight: 600;
    color: #f8fafc;
    line-height: 1.5;
  }

  .notes-input-group {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  .modal-footer {
    display: flex;
    justify-content: flex-end;
    gap: 1rem;
    padding: 1rem 1.5rem;
    background: rgba(0, 0, 0, 0.25);
    border-top: 1px solid rgba(255, 255, 255, 0.05);
  }

  /* Utility Buttons */
  .btn-primary {
    background: var(--dash-accent, #f59e0b);
    color: #000;
    border: none;
    border-radius: 6px;
    padding: 0.5rem 1rem;
    font-weight: 600;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .btn-primary:hover:not(:disabled) { background: #2563eb; }

  .btn-secondary {
    background: rgba(255, 255, 255, 0.08);
    color: #e2e8f0;
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 6px;
    padding: 0.5rem 1rem;
    font-weight: 600;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .btn-secondary:hover:not(:disabled) { background: rgba(255, 255, 255, 0.15); }

  .btn-warning {
    background: #d97706;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 0.5rem 1rem;
    font-weight: 600;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .btn-warning:hover:not(:disabled) { background: #b45309; }

  .btn-success {
    background: #16a34a;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 0.5rem 1.25rem;
    font-weight: 700;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .btn-success:hover { background: #15803d; }

  .btn-danger {
    background: #dc2626;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 0.5rem 1.25rem;
    font-weight: 700;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .btn-danger:hover { background: #b91c1c; }

  .btn-sm { padding: 0.35rem 0.75rem; font-size: 0.75rem; }
  .btn-xs { padding: 0.2rem 0.5rem; font-size: 0.7rem; }
  .full-w { width: 100%; }

  .mono { font-family: monospace; }
  .text-ok { color: #4ade80; }
  .text-danger { color: #f87171; }
  .text-muted { color: #64748b; }
  .text-red { color: #f87171; }
  .text-green { color: #4ade80; }
  .text-blue { color: #60a5fa; }
  .text-xs { font-size: 0.7rem; }

  .flash-banner {
    padding: 0.75rem 1rem;
    background: rgba(34, 197, 94, 0.2);
    border: 1px solid rgba(34, 197, 94, 0.4);
    color: #86efac;
    border-radius: 8px;
    font-size: 0.85rem;
    font-weight: 600;
  }

  .error-banner {
    padding: 0.75rem 1rem;
    background: rgba(239, 68, 68, 0.2);
    border: 1px solid rgba(239, 68, 68, 0.4);
    color: #fca5a5;
    border-radius: 8px;
    font-size: 0.85rem;
  }
</style>
