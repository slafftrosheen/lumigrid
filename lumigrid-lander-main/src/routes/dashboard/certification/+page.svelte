<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import {
    getCertificationPlan,
    getCertificationStatus,
    startCertification,
    startCertificationTest,
    submitCertificationTestResult,
    abortCertification,
    getCertificationReport,
    type CertificationPlanResponse,
    type CertificationStatusResponse,
    type CertificationReportResponse,
    type CertificationProfile,
    type CertificationTestDefinition,
    type CertificationTestResultItem,
    type EvidenceSource,
  } from '$lib/api/certification';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';
  import { postFactoryAbort, getFactoryStatus, type FactoryStatus } from '$lib/api/factory';

  let nodeIp = $state('');
  let loading = $state(true);
  let error = $state('');
  let successMsg = $state('');

  let plan = $state<CertificationPlanResponse | null>(null);
  let status = $state<CertificationStatusResponse | null>(null);
  let factoryStatus = $state<FactoryStatus | null>(null);

  // Profile setup
  let selectedProfile = $state<CertificationProfile>('production_unit');
  let serialNumberInput = $state('');
  let startingSession = $state(false);

  // Test modal & active selection
  let selectedTestId = $state<string | null>(null);
  let activeModal = $state<'test_dialog' | 'report_modal' | 'burnin_modal' | null>(null);
  let testingInProgress = $state(false);
  let submittingResult = $state(false);

  // Operator verification checklist
  let operatorChecks = $state({
    onlyPort: false,
    colorOrder: false,
    pixelLength: false,
    chaseStable: false,
  });

  // Failure dialog
  let isFailureMode = $state(false);
  let failureCategory = $state('no_output');
  let failureNotes = $state('');

  // Instrument dialog
  let measuredValueInput = $state<number | string>('');
  let measurementUnitInput = $state('');
  let measurementMethodInput = $state('');
  let measurementNotes = $state('');

  // Report modal
  let reportData = $state<CertificationReportResponse | null>(null);
  let loadingReport = $state(false);

  // Live node data from store
  const liveSnapshot = $derived($nodeStore.snapshot);
  const liveNode = $derived(liveSnapshot?.node);
  const isOutputTestActive = $derived(
    liveSnapshot?.runtime?.testActive || status?.currentTest !== '' || false
  );

  let pollTimer: ReturnType<typeof setInterval> | null = null;

  const ADDRESSABLE_PORTS = ['A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8'];
  const PWM_PORTS = ['P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8'];

  const FAILURE_CATEGORIES = [
    { value: 'no_output', label: 'No Output / Dead Port' },
    { value: 'wrong_port', label: 'Wrong Port Illuminated' },
    { value: 'wrong_color_order', label: 'Wrong Color Order (e.g. RGB vs GRB)' },
    { value: 'partial_strip', label: 'Partial Strip Illumination' },
    { value: 'flicker', label: 'Flicker / Glitching' },
    { value: 'crosstalk', label: 'Crosstalk on Adjacent Port' },
    { value: 'unstable', label: 'Unstable / Dropped Signal' },
    { value: 'measurement_out_of_tolerance', label: 'Measurement Out of Tolerance' },
    { value: 'other', label: 'Other Hardware Defect' },
  ];

  async function loadData() {
    if (!nodeIp) return;
    try {
      const [p, s, f] = await Promise.all([
        getCertificationPlan(nodeIp),
        getCertificationStatus(nodeIp),
        getFactoryStatus(nodeIp).catch(() => null),
      ]);
      plan = p;
      status = s;
      factoryStatus = f;
      if (s.serialNumber && !serialNumberInput) {
        serialNumberInput = s.serialNumber;
      }
      error = '';
    } catch (e: any) {
      error = e.message || 'Failed to connect to node certification service';
    } finally {
      loading = false;
    }
  }

  onMount(() => {
    nodeIp = getSelectedNodeIp() || '192.168.4.1';
    loadData();
    pollTimer = setInterval(() => {
      if (nodeIp) loadData();
    }, 2000);
  });

  onDestroy(() => {
    if (pollTimer) clearInterval(pollTimer);
  });

  function getTestResult(testId: string): CertificationTestResultItem | undefined {
    return status?.tests?.find((t) => t.id === testId);
  }

  function getTestDefinition(testId: string): CertificationTestDefinition | undefined {
    return plan?.tests?.find((t) => t.id === testId);
  }

  function getPortStatus(port: string): CertificationTestResultItem | undefined {
    const isAddr = port.startsWith('A');
    const testId = isAddr ? `ADDR-${port}` : `PWM-${port}`;
    return getTestResult(testId);
  }

  async function handleStartSession() {
    if (!nodeIp) return;
    if (selectedProfile === 'production_unit' && !serialNumberInput.trim()) {
      error = 'Serial Number is required for ProductionUnit certification!';
      return;
    }
    startingSession = true;
    error = '';
    successMsg = '';
    try {
      const res = await startCertification(nodeIp, selectedProfile, serialNumberInput.trim());
      successMsg = `Certification session ${res.certificationId} started!`;
      await loadData();
    } catch (e: any) {
      error = e.message || 'Failed to start certification session';
    } finally {
      startingSession = false;
    }
  }

  function openTestDialog(testId: string) {
    selectedTestId = testId;
    isFailureMode = false;
    failureCategory = 'no_output';
    failureNotes = '';
    operatorChecks = {
      onlyPort: false,
      colorOrder: false,
      pixelLength: false,
      chaseStable: false,
    };

    const def = getTestDefinition(testId);
    if (def?.requiredEvidence === 'instrument_measurement') {
      measuredValueInput = '';
      measurementUnitInput = def.unit || '';
      measurementMethodInput = '';
      measurementNotes = '';
    }

    activeModal = 'test_dialog';
  }

  async function handleStartTest(testId: string) {
    if (!nodeIp) return;
    testingInProgress = true;
    error = '';
    try {
      await startCertificationTest(nodeIp, testId);
      await loadData();
    } catch (e: any) {
      error = e.message || 'Failed to launch test';
    } finally {
      testingInProgress = false;
    }
  }

  async function handleRecordPass(testId: string) {
    if (!nodeIp) return;
    const def = getTestDefinition(testId);
    submittingResult = true;
    error = '';
    try {
      let measuredVal = 0;
      let unit = '';
      if (def?.requiredEvidence === 'instrument_measurement') {
        measuredVal = parseFloat(String(measuredValueInput));
        if (isNaN(measuredVal)) {
          error = 'Please enter a valid numeric measured value!';
          submittingResult = false;
          return;
        }
        unit = measurementUnitInput;
      }

      await submitCertificationTestResult(nodeIp, {
        testId,
        passed: true,
        evidenceSource: def?.requiredEvidence || 'operator_observation',
        measuredValue: measuredVal,
        unit,
        note: measurementNotes,
      });

      activeModal = null;
      await loadData();
    } catch (e: any) {
      error = e.message || 'Failed to record test result';
    } finally {
      submittingResult = false;
    }
  }

  async function handleRecordFail(testId: string) {
    if (!nodeIp) return;
    if (!failureNotes.trim()) {
      error = 'Failure notes are mandatory when marking a test as failed!';
      return;
    }

    submittingResult = true;
    error = '';
    try {
      const def = getTestDefinition(testId);
      const notePayload = `[${failureCategory}] ${failureNotes.trim()}`;
      await submitCertificationTestResult(nodeIp, {
        testId,
        passed: false,
        evidenceSource: def?.requiredEvidence || 'operator_observation',
        note: notePayload,
      });

      activeModal = null;
      await loadData();
    } catch (e: any) {
      error = e.message || 'Failed to record failure';
    } finally {
      submittingResult = false;
    }
  }

  async function handleEmergencyAbort() {
    if (!nodeIp) return;
    try {
      await abortCertification(nodeIp);
      await postFactoryAbort(nodeIp);
      await loadData();
      successMsg = 'Output test aborted. Controller blacked out safely.';
    } catch (e: any) {
      error = e.message || 'Failed to abort';
    }
  }

  async function handleOpenReport() {
    if (!nodeIp) return;
    loadingReport = true;
    try {
      reportData = await getCertificationReport(nodeIp);
      activeModal = 'report_modal';
    } catch (e: any) {
      error = e.message || 'Failed to load report';
    } finally {
      loadingReport = false;
    }
  }

  function downloadReportJson() {
    if (!reportData) return;
    const blob = new Blob([JSON.stringify(reportData, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `lumigrid-certification-${reportData.nodeId}-${reportData.firmwareVersion}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  const allChecksSatisfied = $derived(
    operatorChecks.onlyPort &&
    operatorChecks.colorOrder &&
    operatorChecks.pixelLength &&
    operatorChecks.chaseStable
  );
</script>

<svelte:head>
  <title>LumiGrid | Hardware Certification Bench</title>
</svelte:head>

<div class="cert-container">
  <!-- Top Emergency Safety Bar (Spec §92) -->
  {#if isOutputTestActive}
    <div class="emergency-bar">
      <div class="emergency-text">
        <span class="pulse-indicator">⚠️</span>
        <strong>PHYSICAL HARDWARE TEST ACTIVE</strong> — Output energized according to factory limits.
      </div>
      <button class="emergency-btn" onclick={handleEmergencyAbort}>
        ⬛ BLACKOUT / ABORT TEST
      </button>
    </div>
  {/if}

  <!-- Header Card -->
  <header class="cert-header">
    <div class="header-top">
      <div>
        <div class="title-row">
          <h1>Hardware Certification Bench</h1>
          {#if status?.stale}
            <span class="cert-badge cert-badge-stale">⚠️ CERTIFICATION STALE</span>
          {:else if status?.state === 'passed'}
            <span class="cert-badge cert-badge-passed">✓ CERTIFIED</span>
          {:else if status?.state === 'failed'}
            <span class="cert-badge cert-badge-failed">✗ FAILED</span>
          {:else if status?.state === 'in_progress'}
            <span class="cert-badge cert-badge-progress">IN PROGRESS</span>
          {:else}
            <span class="cert-badge cert-badge-muted">NOT STARTED</span>
          {/if}
        </div>
        {#if status?.stale}
          <div class="stale-alert">
            Reason: <strong>{status.staleReason || 'Hardware configuration or firmware modified since certification'}</strong>.
            Required verification tests must be re-run to recertify.
          </div>
        {/if}
      </div>

      <div class="header-actions">          <button class="cert-btn cert-btn-outline" onclick={handleOpenReport} disabled={loadingReport || !status}>
          📄 Certification Report
        </button>
        {#if isOutputTestActive}
          <button class="cert-btn cert-btn-danger" onclick={handleEmergencyAbort}>
            Abort Test
          </button>
        {/if}
      </div>
    </div>

    <!-- Metadata Grid -->
    <div class="meta-grid">
      <div class="meta-item">
        <span class="meta-label">Node ID</span>
        <span class="meta-value mono">{status?.nodeId || liveNode?.id || '—'}</span>
      </div>
      <div class="meta-item">
        <span class="meta-label">Serial Number</span>
        <span class="meta-value mono">{status?.serialNumber || 'Unassigned'}</span>
      </div>
      <div class="meta-item">
        <span class="meta-label">Hardware Revision</span>
        <span class="meta-value">{status?.hardwareRevision || 'v1.0'}</span>
      </div>
      <div class="meta-item">
        <span class="meta-label">Firmware Version</span>
        <span class="meta-value mono">{status?.firmwareVersion || '—'}</span>
      </div>
      <div class="meta-item">
        <span class="meta-label">Certification ID</span>
        <span class="meta-value mono">{status?.certificationId || '—'}</span>
      </div>
      <div class="meta-item">
        <span class="meta-label">Plan Version</span>
        <span class="meta-value">v{status?.planVersion || 1}</span>
      </div>
    </div>

    <!-- Progress Overview -->
    {#if status && status.state !== 'not_started'}
      <div class="progress-section">
        <div class="progress-labels">
          <span>Overall Plan Completion</span>
          <span>{status.passed} passed / {status.failed} failed / {status.total} total</span>
        </div>
        <div class="progress-bar-bg">
          <div class="progress-fill-pass" style="width: {(status.passed / (status.total || 1)) * 100}%"></div>
          <div class="progress-fill-fail" style="width: {(status.failed / (status.total || 1)) * 100}%"></div>
        </div>
      </div>
    {/if}

    <!-- Session Start Controls -->
    {#if !status || status.state === 'not_started' || status.state === 'passed' || status.state === 'failed' || status.state === 'incomplete' || status.state === 'invalidated'}
      <div class="session-start-box">
        <div class="session-form">
          <label>
            <span>Target Profile:</span>
            <select bind:value={selectedProfile}>
              <option value="production_unit">Production Unit (Hardware QA & Bring-Up)</option>
              <option value="development">Development (Rapid Bring-Up)</option>
              <option value="release_candidate">Release Candidate (Full Qualification Matrix)</option>
            </select>
          </label>

          {#if selectedProfile === 'production_unit'}
            <label>
              <span>Serial Number (Mandatory):</span>
              <input type="text" placeholder="e.g. SN-LG-2026-0042" bind:value={serialNumberInput} />
            </label>
          {/if}

          <button class="cert-btn cert-btn-primary" onclick={handleStartSession} disabled={startingSession}>
            {startingSession ? 'Initializing Session...' : 'Start New Certification'}
          </button>
        </div>
      </div>
    {/if}

    {#if error}
      <div class="alert alert-error">{error}</div>
    {/if}
    {#if successMsg}
      <div class="alert alert-success">{successMsg}</div>
    {/if}
  </header>

  <!-- Physical Output Topology Grid (Spec §74) -->
  <section class="cert-section">
    <div class="section-header">
      <h2>Physical Channel Topology Bench</h2>
      <p class="section-desc">Independent physical verification for every channel connector. Click any port to run test sequence.</p>
    </div>

    <div class="topology-grid">
      <!-- Addressable Ports Row A1-A8 -->
      <div class="port-row-header">
        <span class="row-tag tag-addr">Addressable (Pixel Lanes A1–A8)</span>
      </div>
      <div class="tiles-row">
        {#each ADDRESSABLE_PORTS as port}
          {@const res = getPortStatus(port)}
          <button
            class="cert-port-tile"
            class:cert-state-passed={res?.state === 'passed'}
            class:cert-state-failed={res?.state === 'failed'}
            class:cert-state-running={res?.state === 'running'}
            class:cert-state-waiting={res?.state === 'waiting_for_operator'}
            class:cert-state-blocked={res?.state === 'blocked'}
            onclick={() => openTestDialog(`ADDR-${port}`)}
          >
            <div class="cert-tile-top">
              <span class="cert-tile-port">{port}</span>
              <span class="cert-tile-pill">
                {#if res?.state === 'passed'}PASS
                {:else if res?.state === 'failed'}FAIL
                {:else if res?.state === 'running'}
                  {#if factoryStatus?.session?.stage}
                    {factoryStatus.session.stage.toUpperCase()}
                  {:else}
                    TESTING
                  {/if}
                {:else if res?.state === 'waiting_for_operator'}WAITING
                {:else if res?.state === 'blocked'}BLOCKED
                {:else}NOT TESTED{/if}
              </span>
            </div>
            <div class="cert-tile-body">
              <span class="cert-tile-type">WS2812/SK6812</span>
              {#if res?.attempts && res.attempts > 1}
                <span class="cert-tile-attempts">Attempts: {res.attempts}</span>
              {/if}
            </div>
          </button>
        {/each}
      </div>

      <!-- PWM Ports Row P1-P8 -->
      <div class="port-row-header mt-4">
        <span class="row-tag tag-pwm">PWM Dimming (Auxiliary P1–P8)</span>
      </div>
      <div class="tiles-row">
        {#each PWM_PORTS as port}
          {@const res = getPortStatus(port)}
          <button
            class="cert-port-tile"
            class:cert-state-passed={res?.state === 'passed'}
            class:cert-state-failed={res?.state === 'failed'}
            class:cert-state-running={res?.state === 'running'}
            class:cert-state-waiting={res?.state === 'waiting_for_operator'}
            class:cert-state-blocked={res?.state === 'blocked'}
            onclick={() => openTestDialog(`PWM-${port}`)}
          >
            <div class="cert-tile-top">
              <span class="cert-tile-port">{port}</span>
              <span class="cert-tile-pill">
                {#if res?.state === 'passed'}PASS
                {:else if res?.state === 'failed'}FAIL
                {:else if res?.state === 'running'}
                  {#if factoryStatus?.session?.stage}
                    {factoryStatus.session.stage.toUpperCase()}
                  {:else}
                    TESTING
                  {/if}
                {:else if res?.state === 'waiting_for_operator'}WAITING
                {:else if res?.state === 'blocked'}BLOCKED
                {:else}NOT TESTED{/if}
              </span>
            </div>
            <div class="cert-tile-body">
              <span class="cert-tile-type">PCA9685 Raw</span>
              {#if res?.measuredValue}
                <span class="cert-tile-measured">{res.measuredValue} {res.unit}</span>
              {/if}
            </div>
          </button>
        {/each}
      </div>
    </div>
  </section>

  <!-- Non-Port Qualification Matrix -->
  <section class="cert-section">
    <div class="section-header">
      <h2>System, Safety, DMX & Recovery Matrix</h2>
      <p class="section-desc">Firmware contract, safe power states, and protocol compliance tests.</p>
    </div>

    <div class="test-table-container">
      <table class="test-table">
        <thead>
          <tr>
            <th>Test ID</th>
            <th>Category</th>
            <th>Title</th>
            <th>Evidence Method</th>
            <th>Status</th>
            <th>Result / Measurement</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {#if plan}
            {#each plan.tests as test}
              {#if !test.id.startsWith('ADDR-A') && !test.id.startsWith('PWM-P')}
                {@const res = getTestResult(test.id)}
                <tr>
                  <td class="mono font-semibold">{test.id}</td>
                  <td><span class="category-tag">{test.category}</span></td>
                  <td>{test.title}</td>
                  <td>
                    <span class="cert-evidence-pill cert-evidence-{test.requiredEvidence}">
                      {test.requiredEvidence.replace('_', ' ')}
                    </span>
                  </td>
                  <td>
                    <span class="cert-status-pill cert-status-{res?.state || 'pending'}">
                      {res?.state || 'pending'}
                    </span>
                  </td>
                  <td class="mono text-sm">
                    {#if res?.measuredValue}
                      {res.measuredValue} {res.unit}
                    {:else if res?.note}
                      {res.note}
                    {:else}
                      —
                    {/if}
                  </td>
                  <td>
                    <button class="cert-btn cert-btn-sm cert-btn-outline" onclick={() => openTestDialog(test.id)}>
                      {res?.state === 'passed' || res?.state === 'failed' ? 'Re-Test' : 'Test'}
                    </button>
                  </td>
                </tr>
              {/if}
            {/each}
          {/if}
        </tbody>
      </table>
    </div>
  </section>
</div>

<!-- Guided Test Interaction Modal (Spec §75, §76, §78) -->
{#if activeModal === 'test_dialog' && selectedTestId}
  {@const def = getTestDefinition(selectedTestId)}
  {@const res = getTestResult(selectedTestId)}
  <div class="modal-backdrop">
    <div class="modal-card">
      <div class="modal-header">
        <div>
          <h3>{selectedTestId}: {def?.title || selectedTestId}</h3>
          <span class="modal-subtitle">Category: {def?.category} • Evidence: {def?.requiredEvidence}</span>
        </div>
        <button class="close-btn" onclick={() => activeModal = null}>✕</button>
      </div>

      <div class="modal-body">
        {#if !isFailureMode}
          <!-- Standard Test View -->
          <div class="test-instruction">
            {#if selectedTestId.startsWith('ADDR-A')}
              <div class="instruction-box">
                <h4>Addressable Output Verification Procedure</h4>
                <ol>
                  <li>Connect fixture to port <strong>{selectedTestId.replace('ADDR-', '')}</strong>.</li>
                  <li>Click <strong>Execute Test Pattern</strong>.</li>
                  <li>Observe sequenced phases: <em>Solid Red → Green → Blue → White → Moving Chase → Blackout</em>.</li>
                  <li>Confirm all checkpoints below before recording PASS.</li>
                </ol>
              </div>

              <!-- Operator Checklist (Spec §25) -->
              <div class="checklist-box">
                <h4>Operator Observation Checklist</h4>
                <label class="check-item">
                  <input type="checkbox" bind:checked={operatorChecks.onlyPort} />
                  <span>Only port <strong>{selectedTestId.replace('ADDR-', '')}</strong> illuminated (no crosstalk on others)</span>
                </label>
                <label class="check-item">
                  <input type="checkbox" bind:checked={operatorChecks.colorOrder} />
                  <span>Color sequence matched exact order: Red, Green, Blue, White</span>
                </label>
                <label class="check-item">
                  <input type="checkbox" bind:checked={operatorChecks.pixelLength} />
                  <span>Entire configured strip length responded without dropped LEDs</span>
                </label>
                <label class="check-item">
                  <input type="checkbox" bind:checked={operatorChecks.chaseStable} />
                  <span>Chase direction is correct and output is clean without flickering</span>
                </label>
              </div>
            {:else if selectedTestId.startsWith('PWM-P')}
              <div class="instruction-box">
                <h4>PWM Auxiliary Output Verification Procedure</h4>
                <ol>
                  <li>Connect test load/meter to port <strong>{selectedTestId.replace('PWM-', '')}</strong>.</li>
                  <li>Click <strong>Execute Test Pattern</strong>.</li>
                  <li>Observe stepped duty levels: <em>0% → 25% → 50% → 75% → 100% → 0%</em>.</li>
                  <li>Confirm smooth transition, full cutoff at 0%, and zero neighboring activity.</li>
                </ol>
              </div>
            {:else if def?.requiredEvidence === 'instrument_measurement'}
              <div class="instruction-box">
                <h4>Calibrated Instrument Measurement Required</h4>
                <p>Use a calibrated multimeter, oscilloscope, or power bench meter. Record physical reading below.</p>
                <div class="measurement-fields">
                  <label>
                    <span>Expected Target:</span>
                    <input type="text" readonly value="{def.expectedValue} {def.unit} (±{def.tolerance} {def.unit})" />
                  </label>
                  <label>
                    <span>Measured Value:</span>
                    <input type="number" step="any" placeholder="Enter measured number" bind:value={measuredValueInput} />
                  </label>
                  <label>
                    <span>Measurement Instrument / Method:</span>
                    <input type="text" placeholder="e.g. Fluke 87V, Rigol DS1054Z Channel 1" bind:value={measurementMethodInput} />
                  </label>
                  <label>
                    <span>Notes:</span>
                    <input type="text" placeholder="Optional notes" bind:value={measurementNotes} />
                  </label>
                </div>
              </div>
            {:else}
              <div class="instruction-box">
                <h4>Automated Firmware Qualification</h4>
                <p>Click execute to trigger firmware invariant assertion and verify subsystem state.</p>
              </div>
            {/if}

            <div class="test-current-status">
              Current Test Status: <strong>{res?.state || 'pending'}</strong>
              {#if res?.attempts} • Attempts: {res.attempts}{/if}
            </div>

            {#if factoryStatus?.session?.stage}
              <div class="test-stage-indicator">
                <span class="stage-pulse">●</span>
                <span>Stage {(factoryStatus.session.stageIndex ?? 0) + 1} / {factoryStatus.session.stageCount ?? 6}: <strong>{factoryStatus.session.stage.toUpperCase()}</strong></span>
              </div>
            {/if}
          </div>

          <div class="modal-footer">              <button class="cert-btn cert-btn-outline" onclick={() => handleStartTest(selectedTestId)} disabled={testingInProgress}>
              {testingInProgress ? 'Running...' : 'Execute Test Pattern'}
            </button>

            <div class="footer-result-actions">
              <button class="cert-btn cert-btn-danger" onclick={() => isFailureMode = true} disabled={submittingResult}>
                Record Failure
              </button>

              <button
                class="cert-btn cert-btn-success"
                onclick={() => handleRecordPass(selectedTestId)}
                disabled={submittingResult || (selectedTestId.startsWith('ADDR-A') && !allChecksSatisfied)}
              >
                {submittingResult ? 'Saving...' : '✓ PASS'}
              </button>
            </div>
          </div>
        {:else}
          <!-- Failure Recording View (Spec §76) -->
          <div class="failure-box">
            <h4>Record Test Failure</h4>
            <p>Specify the physical defect category and provide clear diagnostic notes.</p>

            <label>
              <span>Defect Category:</span>
              <select bind:value={failureCategory}>
                {#each FAILURE_CATEGORIES as cat}
                  <option value={cat.value}>{cat.label}</option>
                {/each}
              </select>
            </label>

            <label>
              <span>Failure Notes (Mandatory):</span>
              <textarea
                placeholder="Explain the observed failure (e.g. green LED flickering at pixel index 45, wrong connector wired)"
                rows="4"
                bind:value={failureNotes}
              ></textarea>
            </label>
          </div>

          <div class="modal-footer">
            <button class="cert-btn cert-btn-outline" onclick={() => isFailureMode = false}>
              Back
            </button>
            <button
              class="cert-btn cert-btn-danger"
              onclick={() => handleRecordFail(selectedTestId)}
              disabled={submittingResult || !failureNotes.trim()}
            >
              {submittingResult ? 'Saving Failure...' : 'Confirm Test Failure'}
            </button>
          </div>
        {/if}
      </div>
    </div>
  </div>
{/if}

<!-- Report Modal (Spec §79, §80) -->
{#if activeModal === 'report_modal' && reportData}
  <div class="modal-backdrop">
    <div class="modal-card modal-wide printable-report">
      <div class="modal-header">
        <div>
          <h3>LumiGrid Hardware Certification Report</h3>
          <span class="modal-subtitle">ID: {reportData.certificationId} • Completed: {new Date(reportData.completedMs).toLocaleString()}</span>
        </div>
        <div class="header-btns">
          <button class="cert-btn cert-btn-sm cert-btn-outline" onclick={() => window.print()}>🖨️ Print</button>
          <button class="cert-btn cert-btn-sm cert-btn-primary" onclick={downloadReportJson}>⬇ Download JSON</button>
          <button class="close-btn" onclick={() => activeModal = null}>✕</button>
        </div>
      </div>

      <div class="modal-body">
        <div class="report-meta-table">
          <div><strong>Node ID:</strong> {reportData.nodeId}</div>
          <div><strong>Serial Number:</strong> {reportData.serialNumber || 'N/A'}</div>
          <div><strong>HW Revision:</strong> {reportData.hardwareRevision}</div>
          <div><strong>Firmware Version:</strong> {reportData.firmwareVersion}</div>
          <div><strong>Profile:</strong> {reportData.profile}</div>
          <div><strong>Overall State:</strong> <span class="badge badge-{reportData.state}">{reportData.state.toUpperCase()}</span></div>
        </div>

        <h4>Verification Matrix</h4>
        <table class="report-table">
          <thead>
            <tr>
              <th>ID</th>
              <th>Category</th>
              <th>Status</th>
              <th>Evidence</th>
              <th>Measurement / Notes</th>
            </tr>
          </thead>
          <tbody>
            {#each reportData.tests as t}
              <tr>
                <td class="cert-mono">{t.id}</td>
                <td>{t.category || ''}</td>
                <td><span class="status-pill status-{t.state}">{t.state}</span></td>
                <td>{t.evidenceSource}</td>
                <td>
                  {#if t.measuredValue}{t.measuredValue} {t.unit}{/if}
                  {#if t.note}{t.note}{/if}
                </td>
              </tr>
            {/each}
          </tbody>
        </table>

        {#if reportData.burnIn?.durationS > 0}
          <h4>Burn-In Endurance Diagnostics</h4>
          <div class="burnin-grid">
            <div>Duration: {reportData.burnIn.durationS}s</div>
            <div>Heap Min: {reportData.burnIn.minFreeHeap} B</div>
            <div>Heap Delta: {(reportData.burnIn.startFreeHeap - reportData.burnIn.endFreeHeap)} B</div>
            <div>Commit Failures: {reportData.burnIn.commitFailures}</div>
            <div>Output Faults: {reportData.burnIn.outputFaults}</div>
          </div>
        {/if}

        <div class="qr-payload-box">
          <h4>Compact Manufacturing QR Payload (Spec §105)</h4>
          <pre class="qr-pre">lumigrid:
node={reportData.nodeId}
serial={reportData.serialNumber}
hw={reportData.hardwareRevision}
fw={reportData.firmwareVersion}
cert={reportData.certificationId}
plan={reportData.planVersion}
result={reportData.state}</pre>
        </div>
      </div>
    </div>
  </div>
{/if}

<style>
  .cert-container {
    max-width: 1200px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
    padding-bottom: 3rem;
  }

  /* Emergency Safety Bar */
  .emergency-bar {
    background: linear-gradient(90deg, #7f1d1d, #991b1b);
    border: 2px solid #ef4444;
    border-radius: 8px;
    padding: 0.75rem 1.25rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
    box-shadow: 0 4px 16px rgba(239, 68, 68, 0.4);
    animation: pulseGlow 2s infinite alternate;
  }
  .emergency-text {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    color: #fef2f2;
    font-size: 0.95rem;
  }
  .pulse-indicator {
    animation: bounce 1s infinite alternate;
  }
  .emergency-btn {
    background: #dc2626;
    color: #fff;
    border: 1px solid #f87171;
    border-radius: 6px;
    padding: 0.5rem 1rem;
    font-weight: 700;
    cursor: pointer;
  }
  .emergency-btn:hover {
    background: #b91c1c;
  }

  /* Cards */
  .cert-header {
    background: linear-gradient(180deg, #111827 0%, #0f172a 100%);
    border: 1px solid #374151;
    border-radius: 12px;
    padding: 1.5rem;
  }
  .cert-section {
    background: #111827;
    border: 1px solid #1f2937;
    border-radius: 12px;
    padding: 1.5rem;
  }
  .header-top {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .title-row {
    display: flex;
    align-items: center;
    gap: 1rem;
  }
  h1 {
    font-size: 1.5rem;
    font-weight: 700;
    color: #f3f4f6;
    margin: 0;
  }
  .stale-alert {
    background: #451a03;
    border: 1px solid #f59e0b;
    color: #fef3c7;
    padding: 0.5rem 0.75rem;
    border-radius: 6px;
    font-size: 0.85rem;
    margin-top: 0.5rem;
  }
  .header-actions {
    display: flex;
    gap: 0.5rem;
  }

  /* Badges */
  .cert-badge {
    padding: 0.25rem 0.6rem;
    border-radius: 9999px;
    font-size: 0.75rem;
    font-weight: 700;
    letter-spacing: 0.05em;
  }
  .cert-badge-passed { background: #065f46; color: #6ee7b7; border: 1px solid #10b981; }
  .cert-badge-failed { background: #7f1d1d; color: #fca5a5; border: 1px solid #ef4444; }
  .cert-badge-progress { background: #1e3a8a; color: #93c5fd; border: 1px solid #0ea5e9; }
  .cert-badge-stale { background: #78350f; color: #fde68a; border: 1px solid #f59e0b; }
  .cert-badge-muted { background: #374151; color: #9ca3af; }

  /* Metadata Grid */
  .meta-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 1rem;
    margin-top: 1.25rem;
    padding-top: 1.25rem;
    border-top: 1px solid #1f2937;
  }
  .meta-item {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .meta-label {
    font-size: 0.75rem;
    color: #9ca3af;
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }
  .meta-value {
    font-size: 0.95rem;
    font-weight: 600;
    color: #e5e7eb;
  }
  .cert-mono {
    font-family: 'SF Mono', 'Fira Code', 'Cascadia Code', monospace;
  }

  /* Progress Section */
  .progress-section {
    margin-top: 1.25rem;
  }
  .progress-labels {
    display: flex;
    justify-content: space-between;
    font-size: 0.85rem;
    color: #9ca3af;
    margin-bottom: 0.4rem;
  }
  .progress-bar-bg {
    height: 8px;
    background: #1f2937;
    border-radius: 9999px;
    display: flex;
    overflow: hidden;
  }
  .progress-fill-pass {
    background: #10b981;
    height: 100%;
    transition: width 0.3s;
  }
  .progress-fill-fail {
    background: #ef4444;
    height: 100%;
    transition: width 0.3s;
  }

  /* Session Start Form */
  .session-start-box {
    margin-top: 1.25rem;
    padding-top: 1.25rem;
    border-top: 1px dashed #374151;
  }
  .session-form {
    display: flex;
    gap: 1rem;
    align-items: flex-end;
    flex-wrap: wrap;
  }
  .session-form label {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
    font-size: 0.85rem;
    color: #d1d5db;
  }
  .session-form select,
  .session-form input {
    background: #1f2937;
    border: 1px solid #374151;
    color: #f3f4f6;
    padding: 0.5rem 0.75rem;
    border-radius: 6px;
    font-size: 0.9rem;
  }

  /* Buttons */
  .cert-btn {
    padding: 0.5rem 1rem;
    border-radius: 6px;
    font-weight: 600;
    font-size: 0.9rem;
    cursor: pointer;
    border: 1px solid transparent;
    transition: background 0.15s;
  }
  .cert-btn-primary { background: #2563eb; color: #fff; border-color: #0ea5e9; }
  .cert-btn-primary:hover { background: #1d4ed8; }
  .cert-btn-outline { background: transparent; color: #d1d5db; border-color: #374151; }
  .cert-btn-outline:hover { background: #1f2937; color: #f3f4f6; }
  .cert-btn-danger { background: #dc2626; color: #fff; border-color: #ef4444; }
  .cert-btn-danger:hover { background: #b91c1c; }
  .cert-btn-success { background: #059669; color: #fff; border-color: #10b981; }
  .cert-btn-success:hover { background: #047857; }
  .cert-btn-sm { padding: 0.3rem 0.6rem; font-size: 0.8rem; }

  /* Alerts */
  .alert {
    padding: 0.75rem 1rem;
    border-radius: 6px;
    margin-top: 1rem;
    font-size: 0.9rem;
  }
  .alert-error { background: #450a0a; border: 1px solid #991b1b; color: #fca5a5; }
  .alert-success { background: #064e3b; border: 1px solid #065f46; color: #6ee7b7; }

  /* Physical Topology Grid */
  .section-header h2 {
    font-size: 1.25rem;
    font-weight: 700;
    color: #f3f4f6;
    margin: 0;
  }
  .section-desc {
    color: #9ca3af;
    font-size: 0.85rem;
    margin-top: 0.25rem;
    margin-bottom: 1.25rem;
  }
  .port-row-header {
    margin-bottom: 0.5rem;
  }
  .row-tag {
    font-size: 0.75rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 0.2rem 0.5rem;
    border-radius: 4px;
  }
  .tag-addr { background: #1e1b4b; color: #a5b4fc; border: 1px solid #4338ca; }
  .tag-pwm { background: #14532d; color: #86efac; border: 1px solid #16a34a; }
  .mt-4 { margin-top: 1.25rem; }

  .tiles-row {
    display: grid;
    grid-template-columns: repeat(8, 1fr);
    gap: 0.75rem;
  }
  @media (max-width: 900px) {
    .tiles-row {
      grid-template-columns: repeat(4, 1fr);
    }
  }
  @media (max-width: 500px) {
    .tiles-row {
      grid-template-columns: repeat(2, 1fr);
    }
  }

  .cert-port-tile {
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 0.75rem;
    text-align: left;
    cursor: pointer;
    transition: all 0.2s;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .cert-port-tile:hover {
    border-color: #64748b;
    transform: translateY(-2px);
  }
  .cert-tile-top {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .cert-tile-port {
    font-size: 1.1rem;
    font-weight: 800;
    color: #f1f5f9;
  }
  .cert-tile-pill {
    font-size: 0.65rem;
    font-weight: 700;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    background: #334155;
    color: #94a3b8;
  }
  .cert-tile-body {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
  }
  .cert-tile-type {
    font-size: 0.75rem;
    color: #64748b;
  }
  .cert-tile-attempts, .cert-tile-measured {
    font-size: 0.7rem;
    color: #38bdf8;
    font-family: monospace;
  }

  /* State styles for tiles */
  .cert-state-passed {
    border-color: #059669;
    background: #064e3b22;
  }
  .cert-state-passed .cert-tile-pill {
    background: #065f46;
    color: #6ee7b7;
  }
  .cert-state-failed {
    border-color: #dc2626;
    background: #7f1d1d22;
  }
  .cert-state-failed .cert-tile-pill {
    background: #991b1b;
    color: #fca5a5;
  }
  .cert-state-running {
    border-color: #0ea5e9;
    background: #1e3a8a33;
    animation: pulseBorder 1.5s infinite;
  }
  .cert-state-running .cert-tile-pill {
    background: #1d4ed8;
    color: #bfdbfe;
  }
  .cert-state-waiting {
    border-color: #eab308;
    background: #713f1233;
  }
  .cert-state-waiting .cert-tile-pill {
    background: #854d0e;
    color: #fde047;
  }

  /* Linked state-pill styles for tile context */
  .cert-state-passed .cert-tile-pill,
  .cert-state-failed .cert-tile-pill,
  .cert-state-running .cert-tile-pill,
  .cert-state-waiting .cert-tile-pill {
    border-radius: 9999px;
    padding: 0.15rem 0.5rem;
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
  }

  /* Table */
  .test-table-container {
    overflow-x: auto;
  }
  .test-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
  }
  .test-table th {
    text-align: left;
    padding: 0.6rem 0.8rem;
    color: #9ca3af;
    border-bottom: 1px solid #374151;
    font-size: 0.75rem;
    text-transform: uppercase;
  }
  .test-table td {
    padding: 0.6rem 0.8rem;
    border-bottom: 1px solid #1f2937;
    color: #e5e7eb;
  }
  .category-tag {
    font-size: 0.7rem;
    background: #1f2937;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    color: #9ca3af;
  }
  .cert-evidence-pill {
    font-size: 0.7rem;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    text-transform: capitalize;
  }
  .cert-evidence-automatic { background: #1e293b; color: #94a3b8; }
  .cert-evidence-operator_observation { background: #2e1065; color: #d8b4fe; }
  .cert-evidence-instrument_measurement { background: #022c22; color: #6ee7b7; }
  .cert-status-pill {
    font-size: 0.7rem;
    font-weight: 700;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    text-transform: uppercase;
  }
  .cert-status-passed { background: #065f46; color: #6ee7b7; }
  .cert-status-failed { background: #991b1b; color: #fca5a5; }
  .cert-status-pending { background: #374151; color: #9ca3af; }
  .cert-status-running { background: #1d4ed8; color: #bfdbfe; }
  .cert-status-blocked { background: #7c2d12; color: #fdba74; }

  /* Modals */
  .modal-backdrop {
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0, 0, 0, 0.75);
    backdrop-filter: blur(4px);
    display: flex;
    justify-content: center;
    align-items: center;
    z-index: 1000;
    padding: 1rem;
  }
  .modal-card {
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 12px;
    max-width: 650px;
    width: 100%;
    max-height: 90vh;
    display: flex;
    flex-direction: column;
    box-shadow: 0 20px 25px -5px rgba(0, 0, 0, 0.5);
  }
  .modal-wide {
    max-width: 950px;
  }
  .modal-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    padding: 1.25rem;
    border-bottom: 1px solid #334155;
  }
  .modal-header h3 {
    margin: 0;
    font-size: 1.15rem;
    color: #f1f5f9;
  }
  .modal-subtitle {
    font-size: 0.8rem;
    color: #94a3b8;
  }
  .close-btn {
    background: transparent;
    border: none;
    color: #94a3b8;
    font-size: 1.2rem;
    cursor: pointer;
  }
  .modal-body {
    padding: 1.25rem;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .modal-footer {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding-top: 1rem;
    border-top: 1px solid #334155;
  }
  .footer-result-actions {
    display: flex;
    gap: 0.75rem;
  }

  /* Modal content styling */
  .instruction-box {
    background: #0f172a;
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 1rem;
  }
  .instruction-box h4 {
    margin-top: 0;
    margin-bottom: 0.5rem;
    color: #38bdf8;
    font-size: 0.95rem;
  }
  .instruction-box ol {
    margin: 0;
    padding-left: 1.2rem;
    color: #cbd5e1;
    font-size: 0.85rem;
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .checklist-box {
    background: #0f172a;
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }
  .test-stage-indicator {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.5rem 0.75rem;
    background: #022c22;
    border: 1px solid #059669;
    border-radius: 6px;
    color: #6ee7b7;
    font-size: 0.85rem;
    font-weight: 600;
  }
  .stage-pulse {
    animation: stagePulse 1.2s infinite ease-in-out;
    color: #10b981;
    font-size: 0.9rem;
  }
  @keyframes stagePulse {
    0%, 100% { opacity: 1; transform: scale(1); }
    50% { opacity: 0.4; transform: scale(0.85); }
  }
  .checklist-box h4 {
    margin: 0;
    color: #e2e8f0;
    font-size: 0.95rem;
  }
  .check-item {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    color: #cbd5e1;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .measurement-fields {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    margin-top: 0.75rem;
  }
  .measurement-fields label {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    font-size: 0.85rem;
    color: #94a3b8;
  }
  .measurement-fields input {
    background: #1e293b;
    border: 1px solid #334155;
    color: #f1f5f9;
    padding: 0.5rem;
    border-radius: 6px;
  }
  .failure-box {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .failure-box select, .failure-box textarea {
    background: #0f172a;
    border: 1px solid #334155;
    color: #f1f5f9;
    padding: 0.6rem;
    border-radius: 6px;
    width: 100%;
  }

  /* Printable Report */
  .report-meta-table {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 0.5rem;
    background: #0f172a;
    padding: 1rem;
    border-radius: 8px;
    font-size: 0.85rem;
  }
  .report-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
  }
  .report-table th, .report-table td {
    padding: 0.5rem;
    border: 1px solid #334155;
  }
  .burnin-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 0.75rem;
    background: #0f172a;
    padding: 1rem;
    border-radius: 8px;
    font-size: 0.85rem;
  }
  .qr-pre {
    background: #0f172a;
    border: 1px dashed #475569;
    padding: 0.75rem;
    border-radius: 6px;
    font-family: monospace;
    font-size: 0.8rem;
    color: #38bdf8;
    white-space: pre-wrap;
  }

  /* Buttons */
  .btn {
    padding: 0.5rem 1rem;
    border-radius: 6px;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    border: 1px solid transparent;
    transition: all 0.15s;
  }
  .btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  @keyframes pulseGlow {
    from { box-shadow: 0 0 10px rgba(239, 68, 68, 0.4); }
    to { box-shadow: 0 0 25px rgba(239, 68, 68, 0.8); }
  }
  @keyframes pulseBorder {
    0% { border-color: #60a5fa; }
    50% { border-color: #93c5fd; }
    100% { border-color: #60a5fa; }
  }
</style>
