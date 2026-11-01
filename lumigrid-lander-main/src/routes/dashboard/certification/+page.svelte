<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import {
    getCertificationPlan, getCertificationStatus, startCertification, startCertificationTest,
    submitCertificationTestResult, abortCertification, getCertificationReport,
    type CertificationPlanResponse, type CertificationStatusResponse, type CertificationReportResponse,
    type CertificationProfile, type CertificationTestDefinition, type CertificationTestResultItem,
  } from '$lib/api/certification';
  import { nodeStore, captureNodeOperation } from '$lib/state/nodeStore';
  import { nodeAuthStore } from '$lib/auth/nodeAuth';
  import { getFactoryStatus, type FactoryStatus } from '$lib/api/factory';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';

  type WorkspaceOwner = { ip: string; isCurrent: () => boolean };
  type TestBinding = { certificationId: string; testId: string; testStartedMs: number; testAttempt: number };
  const STATUS_MAX_AGE_MS = 10000;
  const SERIAL_MAX_BYTES = 31;
  const NOTE_MAX_BYTES = 95;
  const ADDRESSABLE_PORTS = ['A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8'];
  const PWM_PORTS = ['P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8'];
  const FAILURE_CATEGORIES = [
    { value: 'no_output', label: 'No output / dead port' },
    { value: 'wrong_port', label: 'Wrong port illuminated' },
    { value: 'wrong_color_order', label: 'Wrong color order' },
    { value: 'partial_strip', label: 'Partial strip illumination' },
    { value: 'flicker', label: 'Flicker / glitching' },
    { value: 'crosstalk', label: 'Crosstalk on adjacent port' },
    { value: 'unstable', label: 'Unstable / dropped signal' },
    { value: 'measurement_out_of_tolerance', label: 'Measurement out of tolerance' },
    { value: 'other', label: 'Other observed failure' },
  ];

  let nodeIp = $state('');
  let clock = $state(Date.now());
  let loading = $state(true);
  let error = $state('');
  let readError = $state('');
  let factoryError = $state('');
  let successMsg = $state('');
  let plan = $state.raw<CertificationPlanResponse | null>(null);
  let status = $state.raw<CertificationStatusResponse | null>(null);
  let factoryStatus = $state.raw<FactoryStatus | null>(null);
  let statusReadAt = $state(0);
  let statusReadOk = $state(false);
  let factoryReadAt = $state(0);
  let currentRole = $state('unknown');
  let selectedProfile = $state<CertificationProfile>('production_unit');
  let serialNumberInput = $state('');
  let mutation = $state<'session' | 'test' | 'result' | 'abort' | null>(null);
  let abortAwaiting = $state('');
  let pendingEvidence = $state.raw<{ binding: TestBinding; requestedPass: boolean } | null>(null);

  let selectedTestId = $state<string | null>(null);
  let activeModal = $state<'test_dialog' | 'report_modal' | null>(null);
  let dialogBinding = $state.raw<TestBinding | null>(null);
  let dialogChanged = $state('');
  let isFailureMode = $state(false);
  let failureCategory = $state('no_output');
  let failureNotes = $state('');
  let operatorChecks = $state({ onlyPort: false, colorOrder: false, pixelLength: false, chaseStable: false });
  let measuredValueInput = $state<number | string | undefined>('');
  let measurementUnitInput = $state('');
  let measurementMethodInput = $state('');
  let measurementNotes = $state('');
  let reportData = $state.raw<CertificationReportResponse | null>(null);
  let loadingReport = $state(false);

  let disposed = true;
  let workspaceGeneration = 0;
  let dataEpoch = 0;
  let workspaceOwner: ReturnType<typeof captureNodeOperation> | null = null;
  let workspaceSession: ReturnType<typeof nodeAuthStore.getSession> = null;
  let dialogOwner: WorkspaceOwner | null = null;
  let reportOwner: WorkspaceOwner | null = null;
  let readPromise: Promise<boolean> | null = null;

  const statusFresh = $derived(status !== null && statusReadOk && clock - statusReadAt <= STATUS_MAX_AGE_MS);
  const factoryFresh = $derived(factoryStatus !== null && clock - factoryReadAt <= STATUS_MAX_AGE_MS);
  const liveFresh = $derived($nodeStore.route === nodeIp && $nodeStore.snapshot !== null &&
    clock - $nodeStore.lastSnapshotAt <= STATUS_MAX_AGE_MS && ['online', 'degraded'].includes($nodeStore.connection));
  const liveNode = $derived(liveFresh ? $nodeStore.snapshot?.node : null);
  const outputReportedActive = $derived(factoryFresh
    ? factoryStatus?.testActive === true || factoryStatus?.outputsEnabled === true
    : liveFresh && $nodeStore.snapshot?.runtime?.testActive === true);
  const hasSessionWork = $derived(Boolean(status?.certificationId) &&
    (status?.state === 'in_progress' || Boolean(status?.currentTest) || status?.cancellationPending === true ||
      status?.confirmationPending === true || Boolean(abortAwaiting)));
  const busy = $derived(mutation !== null);
  const mutationProblem = $derived.by(() => { clock; return getMutationProblem(); });
  const sessionProblem = $derived.by(() => { clock; return getSessionStartProblem(); });
  const testStartProblem = $derived.by(() => { clock; return selectedTestId ? getTestStartProblem(selectedTestId) : 'Select a test.'; });
  const evidenceProblem = $derived.by(() => { clock; return selectedTestId ? getEvidenceProblem(selectedTestId) : 'Select a test.'; });
  const passProblem = $derived.by(() => { clock; return selectedTestId ? validateResult(selectedTestId, true) : 'Select a test.'; });
  const failProblem = $derived.by(() => { clock; return selectedTestId ? validateResult(selectedTestId, false) : 'Select a test.'; });
  const notePreview = $derived(composeNote(!isFailureMode));

  function utf8Bytes(value: string): number { return new TextEncoder().encode(value).length; }
  function errorMessage(value: unknown): string { return value instanceof Error ? value.message : String(value); }
  function captureWorkspaceOperation(): WorkspaceOwner {
    const owner = captureNodeOperation(nodeIp);
    const generation = workspaceGeneration;
    return { ip: owner.ip, isCurrent: () => !disposed && generation === workspaceGeneration && owner.isCurrent() };
  }
  function getTestResult(testId: string): CertificationTestResultItem | undefined {
    return status?.tests?.find(test => test.id === testId);
  }
  function getTestDefinition(testId: string): CertificationTestDefinition | undefined {
    return plan?.tests?.find(test => test.id === testId);
  }
  function getPortStatus(port: string): CertificationTestResultItem | undefined {
    return getTestResult(`${port.startsWith('A') ? 'ADDR' : 'PWM'}-${port}`);
  }
  function getRequirement(test: CertificationTestDefinition): string {
    const profile = status?.certificationId ? status.profile : selectedProfile;
    return profile === 'development' ? test.reqDev : profile === 'release_candidate' ? test.reqRc : test.reqProd;
  }
  function stateLabel(state: string | undefined): string {
    if (!state) return 'Unavailable';
    return state === 'passed' ? 'Internal checks passed' : state.replaceAll('_', ' ');
  }
  function hasStoredMeasurement(result: CertificationTestResultItem | undefined): boolean {
    return Boolean(result && ['passed', 'failed'].includes(result.state) &&
      (result.attempts > 0 || result.completedMs > 0) && result.unit && Number.isFinite(result.measuredValue));
  }
  function measurementLabel(result: CertificationTestResultItem | undefined): string {
    // The persisted record has no measurement-presence bit. An execution error
    // may leave the initialized value, or follow submitted evidence and a failed
    // output stop. Keep the number visible without claiming either provenance.
    return result?.errorCode === 0 ? 'Recorded measurement' : 'Stored value (measurement unconfirmed)';
  }
  function formatUptime(ms: number | undefined): string {
    return typeof ms === 'number' && Number.isFinite(ms) && ms >= 0 ? `${(ms / 1000).toFixed(3)} s since device boot` : 'Unavailable';
  }
  function reportDuration(report: CertificationReportResponse): string {
    return report.completedMs > 0 && report.completedMs >= report.startedMs
      ? `${((report.completedMs - report.startedMs) / 1000).toFixed(3)} s` : 'Not completed';
  }
  function getMutationProblem(): string {
    if (!nodeIp || disposed || !workspaceOwner?.isCurrent()) return 'Select a connected node to continue.';
    const session = nodeAuthStore.getSession(nodeIp);
    if (!session || session !== workspaceSession || session.role !== 'admin') return 'An active Admin session is required to record evidence or run tests.';
    if (!status || !statusReadOk || Date.now() - statusReadAt > STATUS_MAX_AGE_MS) return 'Fresh acceptance status is required. Refresh the selected node status.';
    if (status.evidenceBinding !== 'session_attempt_v1') return 'This firmware does not support session and attempt binding. Update the node firmware to run acceptance tests; existing evidence is read-only.';
    if (mutation) return 'Waiting for the current command.';
    return '';
  }
  function getSessionStartProblem(): string {
    const problem = getMutationProblem();
    if (problem) return problem;
    if (status?.state === 'in_progress' || status?.currentTest || status?.cancellationPending || status?.confirmationPending || abortAwaiting) return 'Finish or cancel the current session before starting a new one.';
    if (selectedProfile === 'production_unit' && !serialNumberInput.trim()) return 'A serial number is required for the Production Unit profile.';
    if (utf8Bytes(serialNumberInput.trim()) > SERIAL_MAX_BYTES) return `Serial number exceeds ${SERIAL_MAX_BYTES} UTF-8 bytes.`;
    return '';
  }
  function getTestStartProblem(testId: string): string {
    const problem = getMutationProblem();
    if (problem) return problem;
    if (!status?.certificationId || status.state !== 'in_progress' || status.stale) return 'Start a current acceptance session before running a test.';
    if (status.currentTest || status.cancellationPending || status.confirmationPending || abortAwaiting || pendingEvidence) return 'Wait for the current test and output stop to finish.';
    if (!getTestDefinition(testId)) return 'The current test plan is unavailable.';
    if ((getTestResult(testId)?.attempts ?? 0) >= 255) return 'This test has reached 255 attempts. Cancel the current session before starting a new one.';
    return '';
  }
  function makeBinding(testId: string): TestBinding | null {
    const result = getTestResult(testId);
    if (!status?.certificationId || !result) return null;
    return { certificationId: status.certificationId, testId, testStartedMs: result.startedMs, testAttempt: result.attempts };
  }
  function bindingMatches(binding: TestBinding | null): boolean {
    const result = binding && getTestResult(binding.testId);
    return Boolean(binding && result && status?.certificationId === binding.certificationId &&
      result.startedMs === binding.testStartedMs && result.attempts === binding.testAttempt);
  }
  function physicalTarget(testId: string): { kind: string; channel: number } | null {
    if (/^ADDR-A[1-8]$/.test(testId)) return { kind: 'addressable_channel', channel: Number(testId.slice(-1)) - 1 };
    if (/^PWM-P[1-8]$/.test(testId)) return { kind: 'pwm_channel', channel: Number(testId.slice(-1)) + 7 };
    if (testId === 'ADDR-XTALK') return { kind: 'addressable_channel', channel: 0 };
    if (testId === 'PWM-XTALK') return { kind: 'pwm_channel', channel: 8 };
    if (['SAFE-ESTOP-ADDRESSABLE', 'SAFE-ESTOP-PWM', 'SAFE-DISABLE-001'].includes(testId)) return { kind: 'safety_gate', channel: -1 };
    return null;
  }
  function matchingFactorySession(testId: string): boolean {
    const target = physicalTarget(testId);
    const result = getTestResult(testId);
    const session = factoryStatus?.session;
    return Boolean(target && result && status?.currentTest === testId && factoryStatus &&
      Date.now() - factoryReadAt <= STATUS_MAX_AGE_MS && session && session.id > 0 &&
      session.kind === target.kind && session.channel === target.channel &&
      typeof session.startedMs === 'number' && session.startedMs >= result.startedMs);
  }
  function getEvidenceProblem(testId: string): string {
    const problem = getMutationProblem();
    if (problem) return problem;
    if (getTestDefinition(testId)?.requiredEvidence === 'automatic') return 'Automatic evidence is produced by the firmware probe. Operator Pass/Fail is unavailable.';
    if (!dialogOwner?.isCurrent() || selectedTestId !== testId || !bindingMatches(dialogBinding) || dialogChanged) return 'The session or test attempt changed. Close and reopen this test to review the current attempt.';
    if (status?.stale || status?.state !== 'in_progress' || status?.currentTest !== testId || getTestResult(testId)?.state !== 'waiting_for_operator') return 'Evidence can be recorded only for the current test while it is waiting for the operator.';
    if (status.cancellationPending || abortAwaiting) return 'Cancellation is accepted; waiting for the controller to stop.';
    if (status.confirmationPending || pendingEvidence) return 'Evidence is accepted; waiting for output stop confirmation.';
    if (physicalTarget(testId) && (!matchingFactorySession(testId) || factoryStatus?.session?.state !== 'waiting_for_operator')) return 'Matching physical test status is unavailable or not ready. Wait for a fresh factory status.';
    return '';
  }
  function composeNote(passed: boolean): string {
    const parts: string[] = [];
    if (measurementMethodInput.trim()) parts.push(`Method: ${measurementMethodInput.trim()}`);
    if (measurementNotes.trim()) parts.push(`Notes: ${measurementNotes.trim()}`);
    if (!passed && failureNotes.trim()) parts.push(`[${failureCategory}] ${failureNotes.trim()}`);
    return parts.join('; ');
  }
  function validateResult(testId: string, passed: boolean): string {
    const problem = getEvidenceProblem(testId);
    if (problem) return problem;
    const definition = getTestDefinition(testId);
    if (!definition) return 'The current test plan is unavailable.';
    if (!passed && !failureNotes.trim()) return 'Failure notes are required.';
    if (passed && testId.startsWith('ADDR-A') && !Object.values(operatorChecks).every(Boolean)) return 'Confirm each observation before recording a pass.';
    const note = composeNote(passed);
    if (utf8Bytes(note) > NOTE_MAX_BYTES) return `Combined method and notes exceed ${NOTE_MAX_BYTES} UTF-8 bytes.`;
    if (definition.requiredEvidence === 'instrument_measurement') {
      const value = Number(measuredValueInput);
      if (measuredValueInput === undefined || String(measuredValueInput).trim() === '' || !Number.isFinite(value)) return 'Enter a finite numeric measured value, including zero when measured.';
      if (measurementUnitInput !== definition.unit || utf8Bytes(measurementUnitInput) > 7) return `The unit must exactly match the plan: ${definition.unit || '(empty)'}.`;
      if (passed && definition.tolerance > 0 && Math.abs(value - definition.expectedValue) > definition.tolerance && !note) return 'This reading is outside the plan tolerance. Add a note; the node may record it as failed.';
    }
    return '';
  }
  function resetEvidenceInputs() {
    isFailureMode = false; failureCategory = 'no_output'; failureNotes = '';
    operatorChecks = { onlyPort: false, colorOrder: false, pixelLength: false, chaseStable: false };
    measuredValueInput = ''; measurementMethodInput = ''; measurementNotes = '';
    measurementUnitInput = selectedTestId ? getTestDefinition(selectedTestId)?.unit || '' : '';
  }
  function observeStatus(next: CertificationStatusResponse) {
    const previousId = status?.certificationId;
    status = next;
    if (previousId && previousId !== next.certificationId) {
      pendingEvidence = null; abortAwaiting = '';
      reportData = null; reportOwner = null;
      if (activeModal === 'report_modal') activeModal = null;
    }
    if (pendingEvidence && !bindingMatches(pendingEvidence.binding)) {
      pendingEvidence = null; successMsg = '';
      if (!error) error = 'The test attempt changed before this page confirmed the earlier result. Review the current attempt.';
    }
    if (activeModal === 'test_dialog' && dialogBinding && !bindingMatches(dialogBinding)) {
      resetEvidenceInputs(); dialogBinding = null;
      dialogChanged = 'The session or test attempt changed. Previous inputs were cleared. Close and reopen this test to review the current attempt.';
    }
    if (pendingEvidence && bindingMatches(pendingEvidence.binding) && !next.confirmationPending && !next.cancellationPending) {
      const result = getTestResult(pendingEvidence.binding.testId);
      if (result && ['passed', 'failed', 'blocked'].includes(result.state) && next.currentTest !== result.id) {
        successMsg = `Node recorded ${result.id} as ${result.state}.${result.state === 'failed' && pendingEvidence.requestedPass ? ' The requested pass was not confirmed; inspect the recorded evidence and error code.' : ''}`;
        pendingEvidence = null;
      }
    }
    if (abortAwaiting === next.certificationId && !next.cancellationPending && !next.confirmationPending && !next.currentTest && next.state !== 'in_progress' &&
      factoryStatus && Date.now() - factoryReadAt <= STATUS_MAX_AGE_MS && factoryStatus.testActive === false && factoryStatus.outputsEnabled === false) {
      abortAwaiting = ''; pendingEvidence = null;
      successMsg = 'Session cancelled. The controller reports output testing stopped.';
    }
  }
  async function readData(owner: WorkspaceOwner, epoch: number): Promise<boolean> {
    try {
      const [p, s, f] = await Promise.allSettled([
        plan ? Promise.resolve(plan) : getCertificationPlan(owner.ip),
        getCertificationStatus(owner.ip), getFactoryStatus(owner.ip),
      ]);
      if (!owner.isCurrent() || epoch !== dataEpoch) return false;
      if (p.status === 'fulfilled') plan = p.value;
      if (f.status === 'fulfilled' && typeof f.value.testActive === 'boolean' && typeof f.value.outputsEnabled === 'boolean') {
        factoryStatus = f.value; factoryReadAt = Date.now(); factoryError = '';
      } else {
        factoryStatus = null; factoryReadAt = 0;
        factoryError = `Factory output status unavailable: ${f.status === 'rejected' ? errorMessage(f.reason) : 'invalid controller response'}`;
      }
      if (s.status === 'rejected') throw s.reason;
      if (typeof s.value.certificationId !== 'string' || !Array.isArray(s.value.tests) || !s.value.state) throw new Error('The node returned an invalid acceptance status.');
      if (workspaceSession?.nodeId && s.value.nodeId && workspaceSession.nodeId !== s.value.nodeId) throw new Error('Acceptance status belongs to a different node identity.');
      statusReadAt = Date.now(); statusReadOk = true; clock = Date.now();
      observeStatus(s.value);
      if (s.value.serialNumber && !serialNumberInput) serialNumberInput = s.value.serialNumber;
      readError = p.status === 'rejected' ? `Test plan unavailable: ${errorMessage(p.reason)}` : '';
      return true;
    } catch (cause) {
      if (owner.isCurrent() && epoch === dataEpoch) {
        statusReadOk = false;
        readError = `Acceptance status unavailable: ${errorMessage(cause)}`;
      }
      return false;
    } finally {
      if (owner.isCurrent() && epoch === dataEpoch) loading = false;
    }
  }
  function loadData(): Promise<boolean> {
    if (readPromise) return readPromise;
    if (!nodeIp || disposed || !workspaceSession || !workspaceOwner?.isCurrent()) return Promise.resolve(false);
    const request = readData(captureWorkspaceOperation(), dataEpoch);
    readPromise = request;
    void request.finally(() => { if (readPromise === request) readPromise = null; });
    return request;
  }
  async function refreshOwned(owner: WorkspaceOwner): Promise<boolean> {
    if (readPromise) await readPromise;
    if (!owner.isCurrent()) return false;
    return loadData();
  }
  function syncFocusedNode(ip: string) {
    const session = nodeAuthStore.getSession(ip);
    if (ip === nodeIp && session === workspaceSession && workspaceOwner?.isCurrent()) return;
    ++workspaceGeneration; ++dataEpoch;
    nodeIp = ip; workspaceSession = session; workspaceOwner = captureNodeOperation(ip);
    currentRole = session?.role || 'unknown';
    plan = status = factoryStatus = null;
    statusReadAt = factoryReadAt = 0; statusReadOk = false;
    activeModal = selectedTestId = null; dialogBinding = null; dialogOwner = null; dialogChanged = '';
    reportData = null; reportOwner = null; loadingReport = false;
    mutation = null; pendingEvidence = null; abortAwaiting = '';
    error = readError = factoryError = successMsg = ''; serialNumberInput = '';
    resetEvidenceInputs(); loading = Boolean(ip && session);
    if (ip && session) void refreshOwned(captureWorkspaceOperation());
  }
  function beginCommand(kind: NonNullable<typeof mutation>): WorkspaceOwner {
    const owner = captureWorkspaceOperation();
    mutation = kind; ++dataEpoch; statusReadOk = false; error = ''; successMsg = '';
    return owner;
  }
  async function handleStartSession() {
    const problem = getSessionStartProblem();
    if (problem) { error = problem; return; }
    const profile = selectedProfile;
    const serial = serialNumberInput.trim();
    const certificationId = status!.certificationId;
    const owner = beginCommand('session');
    try {
      const result = await startCertification(owner.ip, profile, serial, certificationId);
      if (!owner.isCurrent()) return;
      statusReadOk = false;
      const refreshed = await refreshOwned(owner);
      if (!owner.isCurrent()) return;
      successMsg = refreshed && status?.certificationId === result.certificationId
        ? `Acceptance session ${result.certificationId} started.` : 'Session start accepted. Waiting for current acceptance status.';
    } catch (cause) {
      if (owner.isCurrent()) error = `Session start was not confirmed: ${errorMessage(cause)}`;
    } finally { if (owner.isCurrent()) mutation = null; }
  }
  function openTestDialog(testId: string) {
    if (mutation || !getTestDefinition(testId)) return;
    selectedTestId = testId; dialogOwner = captureWorkspaceOperation(); dialogBinding = makeBinding(testId);
    dialogChanged = ''; resetEvidenceInputs(); activeModal = 'test_dialog';
  }
  function closeDialog() { if (!mutation) activeModal = null; }
  async function handleStartTest(testId: string) {
    const problem = getTestStartProblem(testId);
    if (problem) { error = problem; return; }
    const certificationId = status!.certificationId;
    const owner = beginCommand('test');
    try {
      await startCertificationTest(owner.ip, testId, certificationId);
      if (!owner.isCurrent()) return;
      statusReadOk = false;
      const refreshed = await refreshOwned(owner);
      if (!owner.isCurrent()) return;
      if (refreshed && status?.certificationId !== certificationId) {
        error = 'The acceptance session changed while this test was starting. Inspect the current node evidence.';
        return;
      }
      if (refreshed && status?.certificationId === certificationId && selectedTestId === testId) {
        resetEvidenceInputs(); dialogBinding = makeBinding(testId); dialogChanged = '';
      }
      successMsg = refreshed ? `Current state for ${testId}: ${getTestResult(testId)?.state || 'unavailable'}.` : 'Test start accepted. Waiting for current acceptance status.';
    } catch (cause) {
      if (owner.isCurrent()) error = `Test start was not confirmed: ${errorMessage(cause)}`;
    } finally { if (owner.isCurrent()) mutation = null; }
  }
  async function handleRecordResult(testId: string, passed: boolean) {
    const problem = validateResult(testId, passed);
    if (problem) { error = problem; return; }
    const binding = { ...dialogBinding! };
    const definition = getTestDefinition(testId)!;
    const payload = { ...binding, passed, evidenceSource: definition.requiredEvidence, note: composeNote(passed),
      ...(definition.requiredEvidence === 'instrument_measurement' ? { measuredValue: Number(measuredValueInput), unit: measurementUnitInput } : {}) };
    const owner = beginCommand('result');
    try {
      await submitCertificationTestResult(owner.ip, payload);
      if (!owner.isCurrent()) return;
      pendingEvidence = { binding, requestedPass: passed }; statusReadOk = false;
      successMsg = 'Evidence accepted. Waiting for the node to confirm the final result and output stop.';
      await refreshOwned(owner);
      if (!owner.isCurrent()) return;
      if (pendingEvidence && !bindingMatches(binding)) {
        pendingEvidence = null;
        error = 'Evidence response arrived after the session or attempt changed. Inspect the current node evidence.';
      }
    } catch (cause) {
      if (owner.isCurrent()) error = `Evidence was not confirmed: ${errorMessage(cause)}`;
    } finally { if (owner.isCurrent()) mutation = null; }
  }
  async function handleEmergencyAbort() {
    const problem = getMutationProblem();
    if (problem) { error = problem; return; }
    if (!status?.certificationId || !hasSessionWork) { error = 'There is no current acceptance session to cancel.'; return; }
    const certificationId = status.certificationId;
    const owner = beginCommand('abort');
    try {
      const response = await abortCertification(owner.ip, certificationId);
      if (!owner.isCurrent()) return;
      if (response.accepted === false) throw new Error('The node did not accept cancellation.');
      abortAwaiting = certificationId; statusReadOk = false;
      successMsg = 'Cancellation accepted. Waiting for the controller to confirm output testing stopped.';
      await refreshOwned(owner);
      if (!owner.isCurrent()) return;
    } catch (cause) {
      if (owner.isCurrent()) error = `Cancellation was not confirmed. Retry cancellation for this session: ${errorMessage(cause)}`;
    } finally { if (owner.isCurrent()) mutation = null; }
  }
  async function handleOpenReport() {
    if (loadingReport || mutation || !status?.certificationId || !status.nodeId || !workspaceOwner?.isCurrent()) return;
    const owner = captureWorkspaceOperation();
    const certificationId = status.certificationId;
    const nodeId = status.nodeId;
    loadingReport = true; error = '';
    try {
      const report = await getCertificationReport(owner.ip);
      if (!owner.isCurrent() || status?.certificationId !== certificationId || status?.nodeId !== nodeId) return;
      if (report.certificationId !== certificationId || report.nodeId !== nodeId) throw new Error('The report identity does not match the selected node and session. Refresh status before trying again.');
      reportData = report;
      reportOwner = { ip: owner.ip, isCurrent: () => owner.isCurrent() && status?.certificationId === certificationId && status?.nodeId === nodeId };
      activeModal = 'report_modal';
    } catch (cause) {
      if (owner.isCurrent() && status?.certificationId === certificationId) error = `Report unavailable: ${errorMessage(cause)}`;
    } finally { if (owner.isCurrent()) loadingReport = false; }
  }
  function downloadReportJson() {
    const report = reportData;
    if (!report || !reportOwner?.isCurrent()) return;
    const blob = new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `lumigrid-acceptance-${report.nodeId}-${report.certificationId}.json`.replace(/[^a-zA-Z0-9._-]/g, '_');
    anchor.click(); URL.revokeObjectURL(url);
  }

  onMount(() => {
    disposed = false;
    const unsubscribe = nodeStore.subscribe(state => syncFocusedNode(state.route || ''));
    const authUnsubscribe = nodeAuthStore.subscribe(() => syncFocusedNode($nodeStore.route || ''));
    const timer = setInterval(() => {
      clock = Date.now(); syncFocusedNode($nodeStore.route || '');
      if (!mutation && !readPromise && nodeIp && clock - statusReadAt >= 2000) void loadData();
    }, 1000);
    return () => { unsubscribe(); authUnsubscribe(); clearInterval(timer); };
  });
  onDestroy(() => { disposed = true; ++workspaceGeneration; ++dataEpoch; });
</script>

<svelte:head><title>LumiGrid | Hardware Acceptance Bench</title></svelte:head>

<div class="cert-container">
  {#if hasSessionWork || outputReportedActive}
    <div class="emergency-bar" role="status">
      <div class="emergency-text">
        {#if status?.cancellationPending || abortAwaiting}<strong>Cancellation accepted — waiting for confirmed stop.</strong>
        {:else if status?.confirmationPending}<strong>Evidence accepted — stopping outputs…</strong>
        {:else if status?.currentTest}<strong>Current test: {status.currentTest}</strong>
        {:else}<strong>{hasSessionWork ? 'Acceptance session in progress' : 'Node output activity reported'}</strong>{/if}
        <span>{outputReportedActive ? 'The node reports output testing active.' : factoryFresh ? 'Output state is available from the controller.' : 'Output state is unconfirmed.'}</span>
      </div>
      {#if hasSessionWork}<button type="button" class="cert-btn cert-btn-danger" onclick={handleEmergencyAbort} disabled={Boolean(mutationProblem)}>{mutation === 'abort' ? 'Requesting cancellation…' : 'Cancel Session / Stop Test'}</button>{/if}
    </div>
  {/if}

  <header class="cert-header">
    <div class="header-top">
      <div>
        <div class="title-row">
          <h1>Hardware Acceptance Bench</h1>
          <span class="cert-badge" class:passed={status?.state === 'passed' && !status.stale} class:failed={status?.state === 'failed'}>
            {status?.stale ? 'Evidence stale' : stateLabel(status?.state)}
          </span>
        </div>
        <p class="section-desc">Collect internal hardware test evidence. A passed session means internal checks passed; it does not establish CE approval.</p>
        {#if status?.stale}<p class="alert" role="status">Stored evidence is stale: {status.staleReason || 'The node reports changed firmware or configuration.'}</p>{/if}
      </div>
      <div class="header-actions">
        <button type="button" class="cert-btn cert-btn-outline" onclick={handleOpenReport} aria-busy={loadingReport} aria-disabled={loadingReport || busy || !status?.certificationId || !status?.nodeId} disabled={busy || !status?.certificationId || !status?.nodeId}>{loadingReport ? 'Loading report…' : 'Acceptance Report'}</button>
      </div>
    </div>

    <div class="meta-grid">
      <div class="meta-item"><span class="meta-label">Node ID</span><span class="meta-value mono">{status?.nodeId || liveNode?.id || 'Unknown'}</span></div>
      <div class="meta-item"><span class="meta-label">Serial Number</span><span class="meta-value mono">{status?.serialNumber || 'Unassigned'}</span></div>
      <div class="meta-item"><span class="meta-label">Hardware Revision</span><span class="meta-value">{status?.hardwareRevision || 'Unknown'}</span></div>
      <div class="meta-item"><span class="meta-label">Firmware Version</span><span class="meta-value mono">{status?.firmwareVersion || 'Unavailable'}</span></div>
      <div class="meta-item"><span class="meta-label">Session ID</span><span class="meta-value mono">{status?.certificationId || 'Unavailable'}</span></div>
      <div class="meta-item"><span class="meta-label">Plan Version</span><span class="meta-value">{status?.planVersion ?? plan?.planVersion ?? 'Unavailable'}</span></div>
    </div>

    <div class="status-line" role="status">
      {nodeIp || 'No node selected'} · {currentRole === 'unknown' ? 'No current session' : `${currentRole} session`} ·
      {loading ? 'Loading acceptance status…' : statusFresh ? 'Acceptance status current' : status ? 'Acceptance status stale or unavailable' : 'Acceptance status unavailable'}
      <button type="button" class="cert-btn cert-btn-sm cert-btn-outline" onclick={() => loadData()} disabled={busy || loading || !nodeIp}>Refresh Status</button>
    </div>
    {#if readError}<p class="alert alert-error" role="alert">{readError}</p>{/if}
    {#if factoryError}<p class="alert alert-error" role="alert">{factoryError}. Physical evidence requires matching current output status.</p>{/if}
    {#if mutationProblem && !busy}<p class="alert" role="status">{mutationProblem}</p>{/if}

    {#if status && status.state !== 'not_started'}
      <div class="progress-section">
        <div class="progress-labels"><span>Overall plan completion</span><span>{status.passed} passed / {status.failed} failed / {status.total} total</span></div>
        <div class="progress-bar-bg" aria-hidden="true"><div class="progress-fill-pass" style:width={`${status.passed / (status.total || 1) * 100}%`}></div><div class="progress-fill-fail" style:width={`${status.failed / (status.total || 1) * 100}%`}></div></div>
      </div>
    {/if}

    <div class="session-start-box">
      <div class="session-form">
        <label><span>Target Profile</span><select bind:value={selectedProfile} disabled={busy || hasSessionWork}>
          <option value="production_unit">Production Unit</option><option value="development">Development</option><option value="release_candidate">Release Candidate</option>
        </select></label>
        <label><span>Serial Number{selectedProfile === 'production_unit' ? ' (required)' : ' (optional)'}</span><input type="text" bind:value={serialNumberInput} disabled={busy || hasSessionWork} aria-describedby="serial-limit" /></label>
        <button type="button" class="cert-btn cert-btn-primary" onclick={handleStartSession} disabled={Boolean(sessionProblem)}>{mutation === 'session' ? 'Starting session…' : 'Start New Session'}</button>
      </div>
      <p id="serial-limit" class="field-help">{utf8Bytes(serialNumberInput.trim())}/{SERIAL_MAX_BYTES} UTF-8 bytes after trimming.</p>
      {#if !mutationProblem && sessionProblem}<p class="field-help">{sessionProblem}</p>{/if}
    </div>
    {#if error && activeModal !== 'test_dialog'}<p class="alert alert-error" role="alert">{error}</p>{/if}
    {#if successMsg && activeModal !== 'test_dialog'}<p class="alert alert-success" role="status">{successMsg}</p>{/if}
  </header>

  <section class="cert-section">
    <div class="section-header"><h2>Physical Channel Topology Bench</h2><p class="section-desc">Inspect each connector and its evidence. Start a test from its dialog when the current session is ready.</p></div>
    <div class="topology-grid">
      <h3 class="port-row-header">Addressable (Pixel Lanes A1–A8)</h3>
      <div class="tiles-row">
        {#each ADDRESSABLE_PORTS as port}
          {@const result = getPortStatus(port)}
          <button type="button" class="cert-port-tile" class:passed={result?.state === 'passed'} class:failed={result?.state === 'failed'} onclick={() => openTestDialog(`ADDR-${port}`)} disabled={busy || !getTestDefinition(`ADDR-${port}`)}>
            <span class="cert-tile-top"><span class="cert-tile-port">{port}</span><span class="cert-tile-pill">{result?.state.replaceAll('_', ' ') || 'not tested'}</span></span>
            <span class="cert-tile-body"><span>Addressable output</span>{#if result?.attempts}<span>Attempts: {result.attempts}</span>{/if}</span>
          </button>
        {/each}
      </div>
      <h3 class="port-row-header">PWM Dimming (Auxiliary P1–P8)</h3>
      <div class="tiles-row">
        {#each PWM_PORTS as port}
          {@const result = getPortStatus(port)}
          <button type="button" class="cert-port-tile" class:passed={result?.state === 'passed'} class:failed={result?.state === 'failed'} onclick={() => openTestDialog(`PWM-${port}`)} disabled={busy || !getTestDefinition(`PWM-${port}`)}>
            <span class="cert-tile-top"><span class="cert-tile-port">{port}</span><span class="cert-tile-pill">{result?.state.replaceAll('_', ' ') || 'not tested'}</span></span>
            <span class="cert-tile-body"><span>PWM output</span>{#if hasStoredMeasurement(result)}<span>{measurementLabel(result)}: {result?.measuredValue} {result?.unit}</span>{:else if result?.attempts}<span>Attempts: {result.attempts}</span>{/if}</span>
          </button>
        {/each}
      </div>
    </div>
  </section>

  <section class="cert-section">
    <div class="section-header"><h2>System, Safety, DMX & Recovery Matrix</h2><p class="section-desc">Firmware probes and operator evidence follow the selected session plan.</p></div>
    <div class="test-table-container">
      <table class="test-table">
        <thead><tr><th>Test ID</th><th>Category</th><th>Title</th><th>Evidence Method</th><th>Requirement</th><th>Status</th><th>Result / Measurement</th><th>Actions</th></tr></thead>
        <tbody>
          {#if plan}{#each plan.tests as test}{#if !/^ADDR-A[1-8]$/.test(test.id) && !/^PWM-P[1-8]$/.test(test.id)}
            {@const result = getTestResult(test.id)}
            <tr>
              <td class="mono">{test.id}</td><td>{test.category}</td><td>{test.title}</td><td>{test.requiredEvidence.replaceAll('_', ' ')}</td><td>{getRequirement(test)}</td>
              <td><span class="cert-status-pill" class:passed={result?.state === 'passed'} class:failed={result?.state === 'failed'}>{result?.state.replaceAll('_', ' ') || 'pending'}</span></td>
              <td>{#if hasStoredMeasurement(result)}<div>{measurementLabel(result)}: <span class="mono">{result?.measuredValue} {result?.unit}</span></div>{/if}{#if result?.note}<div>{result.note}</div>{:else if !hasStoredMeasurement(result)}—{/if}</td>
              <td><button type="button" class="cert-btn cert-btn-sm cert-btn-outline" onclick={() => openTestDialog(test.id)} disabled={busy}>Inspect Test</button></td>
            </tr>
          {/if}{/each}{:else}<tr><td colspan="8">{loading ? 'Loading test plan…' : 'The test plan is unavailable.'}</td></tr>{/if}
        </tbody>
      </table>
    </div>
  </section>
</div>

{#if activeModal === 'test_dialog' && selectedTestId}
  {@const definition = getTestDefinition(selectedTestId)}
  {@const result = getTestResult(selectedTestId)}
  <OperatorDialog open title={`${selectedTestId}: ${definition?.title || selectedTestId}`} description={`Category: ${definition?.category || 'Unavailable'} · Evidence: ${definition?.requiredEvidence.replaceAll('_', ' ') || 'Unavailable'}`} {busy} oncancel={closeDialog}>
    <div class="cert-dialog-content">
      <div class="header-actions"><button type="button" data-dialog-cancel class="cert-btn cert-btn-outline" onclick={closeDialog} disabled={busy}>Close</button></div>
      <p class="test-current-status">Current state: <strong>{result?.state.replaceAll('_', ' ') || 'pending'}</strong> · Attempts: {result?.attempts ?? 0} · {definition ? getRequirement(definition) : 'Unavailable'}</p>
      {#if dialogChanged}<p class="alert alert-error" role="alert">{dialogChanged}</p>{/if}
      {#if status?.confirmationPending && status.currentTest === selectedTestId}<p class="alert" role="status">Evidence accepted. Stopping outputs… The final result is pending controller acknowledgement.</p>{/if}
      {#if matchingFactorySession(selectedTestId)}<p class="field-help">Physical test: {factoryStatus?.session?.state.replaceAll('_', ' ')}{factoryStatus?.session?.stage ? ` · Stage: ${factoryStatus.session.stage}` : ''}</p>{/if}
      {#if result?.note}<p class="field-help">Recorded note: {result.note}</p>{/if}
      {#if result?.errorCode}<p class="alert alert-error">Controller error code: {result.errorCode}</p>{/if}
      {#if hasStoredMeasurement(result)}<p class="field-help">{measurementLabel(result)}: {result?.measuredValue} {result?.unit}</p>{/if}

      <div class="instruction-box">
        {#if selectedTestId.startsWith('ADDR-A')}
          <h3>Addressable output verification</h3>
          <ol><li>Connect the fixture to port {selectedTestId.replace('ADDR-', '')}.</li><li>Start the test and observe red, green, blue, white, chase, and blackout phases.</li><li>Wait for operator readiness and confirm the observations below.</li></ol>
        {:else if selectedTestId.startsWith('PWM-P')}
          <h3>PWM output verification</h3><p>Connect a test load to {selectedTestId.replace('PWM-', '')}. Observe the stepped duty levels and final cutoff; verify that neighboring outputs remain inactive.</p>
        {:else if definition?.requiredEvidence === 'automatic'}
          <h3>Firmware probe</h3><p>Run the probe to collect automatic evidence. The firmware owns its result; manual Pass/Fail submission is unavailable.</p>
        {:else if definition?.requiredEvidence === 'instrument_measurement'}
          <h3>Instrument measurement</h3><p>Start the test, then record the measured value and instrument or method using the exact plan unit.</p>
        {:else}<h3>Operator observation</h3><p>Start this test and record your observation when the node reports it is waiting for the operator.</p>{/if}
      </div>

      {#if definition?.requiredEvidence !== 'automatic'}
        {#if selectedTestId.startsWith('ADDR-A') && !isFailureMode}
          <fieldset disabled={Boolean(evidenceProblem)}><legend>Operator observations</legend>
            <label class="check-item"><input type="checkbox" bind:checked={operatorChecks.onlyPort} /><span>Only the selected port illuminated; no crosstalk.</span></label>
            <label class="check-item"><input type="checkbox" bind:checked={operatorChecks.colorOrder} /><span>Color order matched red, green, blue, white.</span></label>
            <label class="check-item"><input type="checkbox" bind:checked={operatorChecks.pixelLength} /><span>The full configured strip length responded.</span></label>
            <label class="check-item"><input type="checkbox" bind:checked={operatorChecks.chaseStable} /><span>Chase direction and output remained stable.</span></label>
          </fieldset>
        {/if}
        {#if definition?.requiredEvidence === 'instrument_measurement'}
          <div class="measurement-fields">
            <p class="field-help">Expected: {definition.expectedValue} {definition.unit} ± {definition.tolerance} {definition.unit}</p>
            <label><span>Measured Value</span><input type="number" step="any" bind:value={measuredValueInput} disabled={Boolean(evidenceProblem)} /></label>
            <label><span>Unit</span><input type="text" bind:value={measurementUnitInput} readonly /></label>
            <label><span>Measurement Instrument / Method</span><input type="text" bind:value={measurementMethodInput} disabled={Boolean(evidenceProblem)} aria-describedby="note-limit" /></label>
          </div>
        {/if}
        <label><span>Notes</span><textarea rows="2" bind:value={measurementNotes} disabled={Boolean(evidenceProblem)} aria-describedby="note-limit"></textarea></label>
        {#if isFailureMode}
          <div class="failure-box">
            <h3>Record test failure</h3>
            <label><span>Defect Category</span><select bind:value={failureCategory} disabled={Boolean(evidenceProblem)}>{#each FAILURE_CATEGORIES as category}<option value={category.value}>{category.label}</option>{/each}</select></label>
            <label><span>Failure Notes</span><textarea rows="3" bind:value={failureNotes} disabled={Boolean(evidenceProblem)} aria-describedby="note-limit"></textarea></label>
          </div>
        {/if}
        <p id="note-limit" class="field-help">Combined stored method and notes: {utf8Bytes(notePreview)}/{NOTE_MAX_BYTES} UTF-8 bytes. Method and notes are saved together.</p>
        {#if isFailureMode ? failProblem : passProblem}<p class="field-help">{isFailureMode ? failProblem : passProblem}</p>{/if}
      {/if}
      {#if error}<p class="alert alert-error" role="alert">{error}</p>{/if}
      {#if successMsg}<p class="alert alert-success" role="status">{successMsg}</p>{/if}
      {#if testStartProblem}<p class="field-help">Start unavailable: {testStartProblem}</p>{/if}
      <div class="modal-footer">
        <button type="button" class="cert-btn cert-btn-outline" onclick={() => handleStartTest(selectedTestId!)} disabled={Boolean(testStartProblem)}>{mutation === 'test' ? 'Starting…' : definition?.requiredEvidence === 'automatic' ? 'Run Firmware Probe' : 'Start Test'}</button>
        {#if definition?.requiredEvidence !== 'automatic'}
          {#if isFailureMode}
            <button type="button" class="cert-btn cert-btn-outline" onclick={() => isFailureMode = false} disabled={busy}>Back</button>
            <button type="button" class="cert-btn cert-btn-danger" onclick={() => handleRecordResult(selectedTestId!, false)} disabled={Boolean(failProblem)}>{mutation === 'result' ? 'Saving…' : 'Confirm Test Failure'}</button>
          {:else}
            <button type="button" class="cert-btn cert-btn-danger" onclick={() => isFailureMode = true} disabled={Boolean(evidenceProblem)}>Record Failure</button>
            <button type="button" class="cert-btn cert-btn-success" onclick={() => handleRecordResult(selectedTestId!, true)} disabled={Boolean(passProblem)}>{mutation === 'result' ? 'Saving…' : 'Record Pass'}</button>
          {/if}
        {/if}
      </div>
    </div>
  </OperatorDialog>
{/if}

{#if activeModal === 'report_modal' && reportData}
  <OperatorDialog open title="LumiGrid Hardware Acceptance Report" description={`Internal test evidence · Session ${reportData.certificationId}`} oncancel={closeDialog}>
    <div class="cert-dialog-content cert-report">
      <div class="header-actions"><button type="button" data-dialog-cancel class="cert-btn cert-btn-outline" onclick={closeDialog}>Close</button><button type="button" class="cert-btn cert-btn-outline" onclick={() => window.print()}>Print</button><button type="button" class="cert-btn cert-btn-primary" onclick={downloadReportJson}>Download JSON</button></div>
      <div class="report-meta-table">
        <div><strong>Node ID:</strong> {reportData.nodeId || 'Unknown'}</div><div><strong>Serial Number:</strong> {reportData.serialNumber || 'Unassigned'}</div>
        <div><strong>Hardware Revision:</strong> {reportData.hardwareRevision || 'Unknown'}</div><div><strong>Session Firmware Version:</strong> {reportData.firmwareVersion || 'Unavailable'}</div>
        <div><strong>Profile:</strong> {reportData.profile || 'Unavailable'}</div><div><strong>Plan Version:</strong> {reportData.planVersion ?? 'Unavailable'}</div>
        <div><strong>Overall State:</strong> {stateLabel(reportData.state)}</div><div><strong>Evidence:</strong> {reportData.stale ? `Stale — ${reportData.staleReason || 'reason unavailable'}` : 'Not marked stale by the node'}</div>
        <div><strong>Started at device uptime:</strong> {formatUptime(reportData.startedMs)}</div><div><strong>Completed at device uptime:</strong> {reportData.completedMs > 0 ? formatUptime(reportData.completedMs) : 'Not completed'}</div>
        <div><strong>Elapsed session duration:</strong> {reportDuration(reportData)}</div>
      </div>
      {#if reportData.reportingFirmwareVersion || reportData.reportingFirmwareElfSha256}
        <div class="instruction-box"><h3>Report exporter</h3><p>This identifies the image exporting this report. It does not prove which image ran the stored tests.</p><p>Exporting firmware version: {reportData.reportingFirmwareVersion || 'Unavailable'}</p><p class="mono">Exporting ELF SHA-256: {reportData.reportingFirmwareElfSha256 || 'Unavailable'}</p></div>
      {/if}
      <h3>Verification Matrix</h3>
      <!-- svelte-ignore a11y_no_noninteractive_tabindex (Keyboard focus is needed to scroll all report columns with arrow keys.) -->
      <div class="test-table-container" role="region" aria-label="Acceptance report verification matrix" tabindex="0"><table class="report-table">
        <thead><tr><th>ID</th><th>Category</th><th>Requirement</th><th>Status</th><th>Evidence</th><th>Measurement / Notes</th></tr></thead>
        <tbody>{#each reportData.tests as test}<tr>
          <td class="mono">{test.id}</td><td>{test.category || 'Unavailable'}</td><td>{test.requirement || 'Unavailable'}</td><td>{test.state.replaceAll('_', ' ')}{#if test.errorCode}<div>Error {test.errorCode}</div>{/if}</td><td>{test.evidenceSource.replaceAll('_', ' ')}</td>
          <td>{#if hasStoredMeasurement(test)}<div>{measurementLabel(test)}: <span class="mono">{test.measuredValue} {test.unit}</span></div>{/if}{#if test.note}<div>{test.note}</div>{:else if !hasStoredMeasurement(test)}—{/if}</td>
        </tr>{/each}</tbody>
      </table></div>
      {#if reportData.burnIn?.durationS > 0}
        <h3>Burn-In Endurance Diagnostics</h3><div class="burnin-grid"><div>Duration: {reportData.burnIn.durationS} s</div><div>Heap minimum: {reportData.burnIn.minFreeHeap} B</div><div>Heap delta: {reportData.burnIn.startFreeHeap - reportData.burnIn.endFreeHeap} B</div><div>Commit failures: {reportData.burnIn.commitFailures}</div><div>Output faults: {reportData.burnIn.outputFaults}</div></div>
      {/if}
      <div class="qr-payload-box"><h3>Compact manufacturing payload</h3><pre>lumigrid:
node={reportData.nodeId}
serial={reportData.serialNumber}
hw={reportData.hardwareRevision}
fw={reportData.firmwareVersion}
cert={reportData.certificationId}
plan={reportData.planVersion}
result={reportData.state}</pre></div>
    </div>
  </OperatorDialog>
{/if}

<style>
  .cert-container { max-width: 1200px; margin: 0 auto; display: flex; flex-direction: column; gap: 24px; padding-bottom: 48px; color: var(--dash-text); }
  .cert-header, .cert-section { min-width: 0; padding: 24px; background: var(--dash-surface); border: 1px solid var(--dash-border); border-radius: var(--dash-radius-lg); }
  .header-top, .title-row, .header-actions, .status-line, .progress-labels, .modal-footer { display: flex; flex-wrap: wrap; align-items: center; gap: 12px; }
  .header-top { align-items: flex-start; justify-content: space-between; }
  .header-top > div { min-width: 0; }
  .title-row { gap: 16px; }
  h1 { margin: 0; font-size: 1.5rem; font-weight: 700; color: var(--dash-text); }
  h2 { margin: 0; font-size: 1.25rem; font-weight: 700; color: var(--dash-text); }
  h3 { margin: 0; font-size: 1rem; font-weight: 700; color: var(--dash-text); }
  p { margin: 0; }
  .section-desc, .field-help { color: var(--dash-muted); font-size: .85rem; line-height: 1.5; overflow-wrap: anywhere; }
  .section-desc { margin-top: 8px; max-width: 68ch; }
  .field-help { margin-top: 8px; }
  .section-header { margin-bottom: 24px; }
  .mono { font-family: 'SF Mono', 'Fira Code', 'Cascadia Code', monospace; overflow-wrap: anywhere; }
  .cert-badge, .cert-status-pill { display: inline-block; padding: 4px 8px; border-radius: var(--dash-radius-sm); border: 1px solid var(--dash-border); color: var(--dash-text); font-size: .75rem; font-weight: 700; }
  .passed { border-color: var(--dash-ok); color: var(--dash-ok); }
  .failed { border-color: var(--dash-danger); color: var(--dash-danger); }
  .meta-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 16px; margin-top: 20px; padding-top: 20px; border-top: 1px solid var(--dash-border); }
  .meta-item { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
  .meta-label { font-size: .75rem; color: var(--dash-muted); text-transform: uppercase; letter-spacing: .05em; }
  .meta-value { font-size: .95rem; font-weight: 600; overflow-wrap: anywhere; }
  .status-line { margin-top: 20px; color: var(--dash-muted); font-size: .85rem; }
  .alert { margin-top: 12px; padding: 12px; background: var(--dash-surface-strong); border: 1px solid var(--dash-border); border-radius: var(--dash-radius-sm); font-size: .85rem; line-height: 1.5; overflow-wrap: anywhere; }
  .alert-error { color: var(--dash-danger); border-color: var(--dash-danger); }
  .alert-success { color: var(--dash-text); }
  .progress-section { margin-top: 20px; }
  .progress-labels { justify-content: space-between; color: var(--dash-muted); font-size: .85rem; margin-bottom: 8px; }
  .progress-bar-bg { height: 8px; display: flex; overflow: hidden; border-radius: var(--dash-radius-sm); background: var(--dash-surface-strong); }
  .progress-fill-pass { height: 100%; background: var(--dash-ok); }
  .progress-fill-fail { height: 100%; background: var(--dash-danger); }
  .session-start-box { margin-top: 20px; padding-top: 20px; border-top: 1px solid var(--dash-border); }
  .session-form { display: flex; gap: 16px; align-items: flex-end; flex-wrap: wrap; }
  label { display: flex; flex-direction: column; gap: 8px; min-width: 0; color: var(--dash-text); font-size: .85rem; }
  input, select, textarea { box-sizing: border-box; max-width: 100%; min-height: var(--dash-control-h); background: var(--dash-surface-strong); border: 1px solid var(--dash-border); border-radius: var(--dash-radius-sm); color: var(--dash-text); padding: 8px 12px; font: inherit; }
  textarea { width: 100%; resize: vertical; }
  input:disabled, select:disabled, textarea:disabled, fieldset:disabled { opacity: .6; }
  input:focus-visible, select:focus-visible, textarea:focus-visible, button:focus-visible, .test-table-container:focus-visible { outline: 2px solid var(--dash-accent); outline-offset: 2px; }
  .cert-btn { min-height: var(--dash-control-h); padding: 8px 16px; border-radius: var(--dash-radius-sm); border: 1px solid var(--dash-border); font: inherit; font-size: .9rem; font-weight: 600; cursor: pointer; color: var(--dash-text); }
  .cert-btn-primary { background: var(--dash-accent); border-color: var(--dash-accent); color: #000; }
  .cert-btn-primary:hover:not(:disabled) { background: var(--dash-accent-hover); }
  .cert-btn-outline { background: var(--dash-surface); }
  .cert-btn-outline:hover:not(:disabled) { background: var(--dash-surface-strong); }
  .cert-btn-danger { background: var(--dash-surface-strong); color: var(--dash-danger); border-color: var(--dash-danger); }
  .cert-btn-success { background: var(--dash-surface-strong); color: var(--dash-ok); border-color: var(--dash-ok); }
  button:disabled { opacity: .5; cursor: not-allowed; }
  .cert-btn-sm { font-size: .8rem; padding: 8px 12px; }
  .emergency-bar { background: var(--dash-surface-strong); border: 1px solid var(--dash-danger); border-radius: var(--dash-radius-md); padding: 16px 20px; display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 16px; }
  .emergency-text { display: flex; flex-direction: column; gap: 8px; font-size: .9rem; }
  .emergency-text span { color: var(--dash-muted); }
  .topology-grid { display: flex; flex-direction: column; gap: 16px; }
  .port-row-header { color: var(--dash-text); font-size: .9rem; }
  .tiles-row { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
  .cert-port-tile { min-width: 0; min-height: 96px; display: flex; flex-direction: column; gap: 16px; text-align: left; background: var(--dash-surface-strong); border: 1px solid var(--dash-border); border-radius: var(--dash-radius-md); padding: 16px; color: var(--dash-text); cursor: pointer; font: inherit; }
  .cert-port-tile.passed { border-color: var(--dash-ok); }
  .cert-port-tile.failed { border-color: var(--dash-danger); }
  .cert-tile-top { display: flex; gap: 8px; align-items: center; justify-content: space-between; flex-wrap: wrap; }
  .cert-tile-port { font-size: 1.25rem; font-weight: 700; }
  .cert-tile-pill { font-size: .75rem; text-transform: uppercase; }
  .cert-tile-body { display: flex; flex-direction: column; gap: 4px; color: var(--dash-muted); font-size: .8rem; }
  .test-table-container { max-width: 100%; min-width: 0; overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; font-size: .85rem; }
  th, td { text-align: left; padding: 12px; border-bottom: 1px solid var(--dash-border); vertical-align: top; }
  th { color: var(--dash-muted); font-size: .75rem; font-weight: 700; }
  td { color: var(--dash-text); overflow-wrap: anywhere; min-width: 80px; }
  :global(.dialog-card:has(.cert-dialog-content)) { width: min(640px, 100%); max-height: calc(100dvh - 32px); min-width: 0; box-sizing: border-box; }
  :global(.dialog-card:has(.cert-report)) { width: min(1000px, 100%); }
  .cert-dialog-content { min-width: 0; min-height: 0; overflow-y: auto; display: flex; flex-direction: column; gap: 16px; padding: 4px; color: var(--dash-text); }
  .cert-dialog-content > * { flex-shrink: 0; }
  .cert-dialog-content .field-help, .cert-dialog-content .alert { margin-top: 0; }
  .test-current-status { font-size: .85rem; color: var(--dash-muted); }
  .instruction-box, fieldset, .failure-box { min-width: 0; margin: 0; padding: 16px; border: 1px solid var(--dash-border); border-radius: var(--dash-radius-sm); background: var(--dash-surface); }
  .instruction-box p { margin-top: 8px; font-size: .85rem; color: var(--dash-muted); line-height: 1.5; }
  .instruction-box ol { padding-left: 20px; margin: 8px 0 0; font-size: .85rem; line-height: 1.5; }
  fieldset { display: flex; flex-direction: column; gap: 12px; }
  legend { font-size: .85rem; padding: 0 4px; }
  .check-item { flex-direction: row; align-items: center; gap: 12px; }
  .check-item input { min-height: 20px; width: 20px; flex-shrink: 0; accent-color: var(--dash-accent); }
  .measurement-fields, .failure-box { display: flex; flex-direction: column; gap: 16px; }
  .modal-footer { justify-content: flex-end; padding-top: 16px; border-top: 1px solid var(--dash-border); }
  .report-meta-table, .burnin-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 12px 24px; font-size: .85rem; }
  .report-meta-table > div { overflow-wrap: anywhere; }
  .report-table { min-width: 720px; }
  .qr-payload-box pre { white-space: pre-wrap; overflow-wrap: anywhere; color: var(--dash-muted); font-size: .8rem; margin: 12px 0 0; }
  @media (max-width: 700px) {
    .cert-header, .cert-section { padding: 16px; }
    .tiles-row { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .cert-port-tile { padding: 12px; }
    .report-meta-table, .burnin-grid { grid-template-columns: minmax(0, 1fr); }
    .session-form, .session-form label { width: 100%; }
    .session-form .cert-btn { width: 100%; }
    .modal-footer { justify-content: stretch; }
    .modal-footer .cert-btn { flex: 1 1 160px; }
  }
  @media print {
    .cert-container { display: none; }
    .cert-report .header-actions { display: none; }
    .cert-report { overflow: visible; }
    .cert-report .test-table-container { overflow: visible; }
    .report-table { min-width: 0; }
    :global(.dialog-card:has(.cert-report)) { max-height: none; }
    :global(.dialog-backdrop:has(.cert-report)) { position: static; padding: 0; }
  }
</style>
