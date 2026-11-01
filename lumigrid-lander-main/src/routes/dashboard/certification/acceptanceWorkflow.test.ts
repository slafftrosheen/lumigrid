import { afterEach, describe, expect, it, vi } from 'vitest';
import { loadPageFunctions } from '$lib/testing/pageScriptHarness';

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(done => { resolve = done; });
  return { promise, resolve };
}
const definition = (id = 'POWER-LIMIT-001', requiredEvidence = 'instrument_measurement') => ({
  id, requiredEvidence, category: 'POWER', title: 'Power measurement', unit: 'mW', expectedValue: 100, tolerance: 5,
  reqDev: 'optional', reqProd: 'required', reqRc: 'required',
});
const result = (id = 'POWER-LIMIT-001', state = 'waiting_for_operator') => ({
  id, state, evidenceSource: 'instrument_measurement', startedMs: 12000, completedMs: 0, measuredValue: 0,
  unit: 'mW', note: '', attempts: 1, errorCode: 0,
});
const status = () => ({
  certificationId: 'cert-a', nodeId: 'node-a', state: 'in_progress', profile: 'production_unit', stale: false,
  evidenceBinding: 'session_attempt_v1', cancellationPending: false, confirmationPending: false,
  currentTest: 'POWER-LIMIT-001', serialNumber: 'SN-A', tests: [result()],
});
function workspace() {
  let generation = 0;
  let selectedIp = 'node-a';
  let session = { role: 'admin', nodeId: 'node-a' };
  const c: Record<string, any> = {
    nodeIp: selectedIp, disposed: false, workspaceGeneration: 1, dataEpoch: 1, workspaceSession: session,
    clock: Date.now(), statusReadAt: Date.now(), statusReadOk: true, factoryReadAt: Date.now(),
    STATUS_MAX_AGE_MS: 10000, SERIAL_MAX_BYTES: 31, NOTE_MAX_BYTES: 95,
    mutation: null, loading: false, currentRole: 'admin', error: '', readError: '', factoryError: '', successMsg: '',
    status: status(), plan: { tests: [definition(), definition('BOOT-001', 'automatic')] },
    factoryStatus: { testActive: false, outputsEnabled: false, session: null }, readPromise: null,
    selectedProfile: 'production_unit', serialNumberInput: ' SN-NEW ', abortAwaiting: '', pendingEvidence: null,
    activeModal: 'test_dialog', selectedTestId: 'POWER-LIMIT-001', dialogBinding: null, dialogChanged: '',
    isFailureMode: false, failureCategory: 'other', failureNotes: 'No load',
    operatorChecks: { onlyPort: true, colorOrder: true, pixelLength: true, chaseStable: true },
    measuredValueInput: 0, measurementUnitInput: 'mW', measurementMethodInput: 'scope CH1', measurementNotes: 'Zero load',
    reportData: null, reportOwner: null, loadingReport: false,
    nodeAuthStore: { getSession: () => session },
    captureNodeOperation: (ip: string) => {
      const oldGeneration = generation; const oldSession = session;
      return { ip, isCurrent: () => oldGeneration === generation && oldSession === session && ip === selectedIp };
    },
    getCertificationPlan: vi.fn(async () => c.plan),
    getCertificationStatus: vi.fn(async () => c.status),
    getFactoryStatus: vi.fn(async () => ({ testActive: false, outputsEnabled: false, session: null })),
    startCertification: vi.fn(async () => ({ certificationId: 'cert-new' })),
    startCertificationTest: vi.fn(async () => ({ status: 'started' })),
    submitCertificationTestResult: vi.fn(async () => ({ status: 'recorded', overallState: 'in_progress' })),
    abortCertification: vi.fn(async () => ({ status: 'cancellation_requested', accepted: true, cancellationRequested: true })),
    getCertificationReport: vi.fn(async () => ({ certificationId: 'cert-a', nodeId: 'node-a', tests: [] })),
  };
  c.workspaceOwner = c.captureNodeOperation(selectedIp);
  c.dialogOwner = c.captureNodeOperation(selectedIp);
  Object.defineProperty(c, 'hasSessionWork', { get: () => Boolean(c.status?.certificationId) &&
    (c.status.state === 'in_progress' || c.status.currentTest || c.status.cancellationPending || c.status.confirmationPending || c.abortAwaiting) });
  const page = loadPageFunctions(new URL('./+page.svelte', import.meta.url), [
    'utf8Bytes', 'errorMessage', 'captureWorkspaceOperation', 'getTestResult', 'getTestDefinition', 'getPortStatus',
    'getRequirement', 'stateLabel', 'hasStoredMeasurement', 'measurementLabel', 'formatUptime', 'reportDuration', 'getMutationProblem',
    'getSessionStartProblem', 'getTestStartProblem', 'makeBinding', 'bindingMatches', 'physicalTarget',
    'matchingFactorySession', 'getEvidenceProblem', 'composeNote', 'validateResult', 'resetEvidenceInputs',
    'observeStatus', 'readData', 'loadData', 'refreshOwned', 'syncFocusedNode', 'beginCommand', 'handleStartSession',
    'openTestDialog', 'closeDialog', 'handleStartTest', 'handleRecordResult', 'handleEmergencyAbort', 'handleOpenReport',
  ], c);
  c.dialogBinding = page.makeBinding(c.selectedTestId);
  return { c, page,
    changeNode(ip = 'node-b') { ++generation; selectedIp = ip; c.nodeIp = ip; },
    replaceSession() { session = { role: 'admin', nodeId: 'node-a' }; },
  };
}
afterEach(() => { vi.restoreAllMocks(); vi.useRealTimers(); });

describe('acceptance production page ownership and polling', () => {
  it('does not read or mutate a guessed fallback address and requires current Admin, compatible, fresh status', async () => {
    const { c, page } = workspace();
    c.nodeIp = '';
    await page.loadData(); await page.handleRecordResult('POWER-LIMIT-001', false);
    expect(c.getCertificationStatus).not.toHaveBeenCalled(); expect(c.submitCertificationTestResult).not.toHaveBeenCalled();
    c.nodeIp = 'node-a'; c.workspaceSession.role = 'operator';
    expect(page.getMutationProblem()).toMatch(/Admin/);
    c.workspaceSession.role = 'admin'; c.status.evidenceBinding = undefined;
    expect(page.getMutationProblem()).toMatch(/read-only/);
    c.status.evidenceBinding = 'session_attempt_v1'; c.statusReadAt -= 10001;
    expect(page.getMutationProblem()).toMatch(/Fresh acceptance status/);
  });

  it('shares one in-flight read and discards an A→B→A response despite the same final IP', async () => {
    const { c, page, changeNode } = workspace();
    const read = deferred<any>(); c.getCertificationStatus.mockReturnValueOnce(read.promise);
    const first = page.loadData(); const second = page.loadData();
    expect(first).toBe(second); expect(c.getCertificationStatus).toHaveBeenCalledTimes(1);
    changeNode(); changeNode('node-a'); c.status = null;
    read.resolve({ ...status(), certificationId: 'obsolete' });
    expect(await first).toBe(false); expect(c.status).toBeNull();
  });

  it.each(['session', 'disposal'])('discards reads after %s ownership changes', async reason => {
    const { c, page, replaceSession } = workspace();
    const read = deferred<any>(); c.getCertificationStatus.mockReturnValueOnce(read.promise);
    const pending = page.loadData();
    if (reason === 'session') replaceSession(); else c.disposed = true;
    c.status = null; read.resolve(status()); await pending;
    expect(c.status).toBeNull();
  });

  it('keeps a command failure through successful polling and exposes failed factory status separately', async () => {
    const { c, page } = workspace();
    c.submitCertificationTestResult.mockRejectedValueOnce(new Error('operator evidence denied'));
    await page.handleRecordResult('POWER-LIMIT-001', false);
    expect(c.error).toMatch(/operator evidence denied/);
    c.getFactoryStatus.mockRejectedValueOnce(new Error('factory offline'));
    await page.loadData();
    expect(c.error).toMatch(/operator evidence denied/);
    expect(c.statusReadOk).toBe(true); expect(c.factoryStatus).toBeNull();
    expect(c.factoryError).toMatch(/factory offline/);
  });

  it('captures immutable session input and never follows a delayed command onto the next node', async () => {
    const { c, page, changeNode } = workspace();
    c.status.state = 'incomplete'; c.status.currentTest = '';
    const request = deferred<any>(); c.startCertification.mockReturnValueOnce(request.promise);
    const pending = page.handleStartSession();
    c.selectedProfile = 'development'; c.serialNumberInput = 'DIFFERENT';
    changeNode(); c.successMsg = 'new workspace';
    request.resolve({ certificationId: 'late-a' }); await pending;
    expect(c.startCertification).toHaveBeenCalledWith('node-a', 'production_unit', 'SN-NEW', 'cert-a');
    expect(c.successMsg).toBe('new workspace'); expect(c.getCertificationStatus).not.toHaveBeenCalled();
  });

  it('enforces UTF-8 serial length after trimming, before a start mutation', async () => {
    const { c, page } = workspace(); c.status.state = 'incomplete'; c.status.currentTest = '';
    c.serialNumberInput = 'é'.repeat(16);
    await page.handleStartSession(); expect(c.error).toMatch(/31 UTF-8 bytes/); expect(c.startCertification).not.toHaveBeenCalled();
    c.serialNumberInput = '  ' + 'é'.repeat(15) + 'a  ';
    expect(page.getSessionStartProblem()).toBe('');
  });
});

describe('acceptance production evidence workflow', () => {
  it('submits zero on failure with exact session/attempt binding and persists the instrument method', async () => {
    const { c, page } = workspace();
    c.getCertificationStatus.mockResolvedValueOnce({ ...status(), currentTest: '', tests: [{ ...result(), state: 'failed', completedMs: 13000 }] });
    await page.handleRecordResult('POWER-LIMIT-001', false);
    expect(c.submitCertificationTestResult).toHaveBeenCalledWith('node-a', {
      certificationId: 'cert-a', testId: 'POWER-LIMIT-001', testStartedMs: 12000, testAttempt: 1,
      passed: false, evidenceSource: 'instrument_measurement', measuredValue: 0, unit: 'mW',
      note: 'Method: scope CH1; Notes: Zero load; [other] No load',
    });
    expect(c.successMsg).toBe('Node recorded POWER-LIMIT-001 as failed.');
  });

  it.each([true, false])('validates finite measurement, exact unit and byte-size for passed=%s', async passed => {
    const { c, page } = workspace();
    for (const value of ['', undefined, Infinity, '12junk']) {
      c.measuredValueInput = value;
      expect(page.validateResult('POWER-LIMIT-001', passed)).toMatch(/finite numeric/);
    }
    c.measuredValueInput = 0; c.measurementUnitInput = 'W';
    expect(page.validateResult('POWER-LIMIT-001', passed)).toMatch(/exactly match/);
    c.measurementUnitInput = 'mW'; c.measurementMethodInput = '測'.repeat(32);
    expect(page.validateResult('POWER-LIMIT-001', passed)).toMatch(/95 UTF-8 bytes/);
    await page.handleRecordResult('POWER-LIMIT-001', passed);
    expect(c.submitCertificationTestResult).not.toHaveBeenCalled();
  });

  it('invalidates the dialog and clears old inputs when a new attempt is observed', async () => {
    const { c, page } = workspace();
    page.observeStatus({ ...status(), tests: [{ ...result(), startedMs: 15000, attempts: 2 }] });
    expect(c.dialogBinding).toBeNull(); expect(c.dialogChanged).toMatch(/Previous inputs were cleared/);
    expect(c.measuredValueInput).toBe(''); expect(c.measurementMethodInput).toBe('');
    await page.handleRecordResult('POWER-LIMIT-001', false); expect(c.submitCertificationTestResult).not.toHaveBeenCalled();
  });

  it('releases obsolete pending evidence when another client advances the attempt, without erasing a command error', () => {
    const { c, page } = workspace();
    const firstBinding = page.makeBinding('POWER-LIMIT-001');
    c.pendingEvidence = { binding: firstBinding, requestedPass: true };
    c.successMsg = 'Evidence accepted. Waiting for output stop.';
    const next = { ...status(), tests: [{ ...result(), startedMs: 15000, attempts: 2 }] };
    page.observeStatus(next);
    expect(c.pendingEvidence).toBeNull(); expect(c.successMsg).toBe('');
    expect(c.error).toMatch(/attempt changed before this page confirmed the earlier result/);
    page.openTestDialog('POWER-LIMIT-001');
    expect(page.getEvidenceProblem('POWER-LIMIT-001')).toBe('');
    c.status.currentTest = '';
    expect(page.getTestStartProblem('POWER-LIMIT-001')).toBe('');
    c.pendingEvidence = { binding: firstBinding, requestedPass: true }; c.error = 'Cancellation was not confirmed';
    page.observeStatus(next);
    expect(c.pendingEvidence).toBeNull(); expect(c.error).toBe('Cancellation was not confirmed');
  });

  it('rejects manual automatic evidence and evidence for non-current tests', async () => {
    const { c, page } = workspace();
    page.openTestDialog('BOOT-001');
    await page.handleRecordResult('BOOT-001', false);
    expect(c.error).toMatch(/Automatic evidence/); expect(c.submitCertificationTestResult).not.toHaveBeenCalled();
    page.openTestDialog('POWER-LIMIT-001'); c.status.currentTest = 'BOOT-001';
    expect(page.getEvidenceProblem('POWER-LIMIT-001')).toMatch(/only for the current test/);
  });

  it('blocks an exhausted 255th test attempt and explains how to begin a new session', async () => {
    const { c, page } = workspace();
    c.status.currentTest = ''; c.status.tests[0].attempts = 255;
    expect(page.getTestStartProblem('POWER-LIMIT-001')).toMatch(/255 attempts.*Cancel the current session/);
    await page.handleStartTest('POWER-LIMIT-001');
    expect(c.startCertificationTest).not.toHaveBeenCalled();
  });

  it('requires fresh matching physical runner state, then blocks evidence during output confirmation', () => {
    const { c, page } = workspace();
    c.plan.tests = [definition('SAFE-ESTOP-PWM')];
    c.status.currentTest = 'SAFE-ESTOP-PWM'; c.status.tests = [result('SAFE-ESTOP-PWM')];
    page.openTestDialog('SAFE-ESTOP-PWM');
    expect(page.getEvidenceProblem('SAFE-ESTOP-PWM')).toMatch(/Matching physical test status/);
    c.factoryStatus.session = { id: 2, kind: 'safety_gate', channel: -1, state: 'waiting_for_operator', startedMs: 12002 };
    expect(page.getEvidenceProblem('SAFE-ESTOP-PWM')).toBe('');
    c.factoryStatus.session.startedMs = 11999;
    expect(page.getEvidenceProblem('SAFE-ESTOP-PWM')).toMatch(/Matching physical/);
    c.factoryStatus.session.startedMs = 12002; c.status.confirmationPending = true;
    expect(page.getEvidenceProblem('SAFE-ESTOP-PWM')).toMatch(/output stop confirmation/);
    expect(page.getMutationProblem()).toBe(''); // A bound Abort stays available.
  });

  it('does not announce a pass on receipt, and reports a node-converted failed result honestly', async () => {
    const { c, page } = workspace();
    c.measuredValueInput = 500;
    c.getCertificationStatus.mockResolvedValueOnce({ ...status(), confirmationPending: true, tests: [{ ...result(), state: 'running' }] });
    await page.handleRecordResult('POWER-LIMIT-001', true);
    expect(c.pendingEvidence).not.toBeNull(); expect(c.successMsg).toMatch(/Waiting.*final result/);
    expect(c.successMsg).not.toMatch(/recorded.*passed/);
    page.observeStatus({ ...status(), currentTest: '', tests: [{ ...result(), state: 'failed', measuredValue: 500, completedMs: 18000 }] });
    expect(c.successMsg).toMatch(/recorded.*failed.*pass was not confirmed/);
    expect(c.pendingEvidence).toBeNull();
  });

  it('sends only the bound abort and waits for inactive output acknowledgement', async () => {
    const { c, page } = workspace();
    c.getCertificationStatus.mockResolvedValueOnce({ ...status(), state: 'incomplete', cancellationPending: true });
    c.getFactoryStatus.mockResolvedValueOnce({ testActive: true, outputsEnabled: true });
    await page.handleEmergencyAbort();
    expect(c.abortCertification).toHaveBeenCalledWith('node-a', 'cert-a');
    expect(c.abortAwaiting).toBe('cert-a'); expect(c.successMsg).toMatch(/Waiting.*stopped/);
    page.observeStatus({ ...status(), state: 'incomplete', currentTest: '' });
    expect(c.abortAwaiting).toBe('cert-a');
    c.factoryStatus = { testActive: false, outputsEnabled: false };
    page.observeStatus({ ...status(), state: 'incomplete', currentTest: '' });
    expect(c.abortAwaiting).toBe(''); expect(c.successMsg).toMatch(/controller reports output testing stopped/);
  });
});

describe('acceptance production report ownership and formatting', () => {
  it('rejects a report for another node/session', async () => {
    const { c, page } = workspace();
    c.activeModal = null; c.getCertificationReport.mockResolvedValueOnce({ certificationId: 'other', nodeId: 'node-a' });
    await page.handleOpenReport();
    expect(c.reportData).toBeNull(); expect(c.error).toMatch(/report identity/);
  });
  it('discards an async report after the selected session changes', async () => {
    const { c, page } = workspace(); const report = deferred<any>();
    c.activeModal = null; c.getCertificationReport.mockReturnValueOnce(report.promise);
    const pending = page.handleOpenReport(); c.status.certificationId = 'new-cert';
    report.resolve({ certificationId: 'cert-a', nodeId: 'node-a' }); await pending;
    expect(c.reportData).toBeNull(); expect(c.activeModal).toBeNull();
  });
  it('formats device uptime and zero evidence without treating a pending zero as a measurement', () => {
    const { page } = workspace();
    expect(page.formatUptime(12345)).toBe('12.345 s since device boot');
    expect(page.reportDuration({ startedMs: 12000, completedMs: 18000 })).toBe('6.000 s');
    expect(page.reportDuration({ startedMs: 12000, completedMs: 0 })).toBe('Not completed');
    expect(page.hasStoredMeasurement(result('POWER-LIMIT-001', 'pending'))).toBe(false);
    expect(page.hasStoredMeasurement({ ...result(), state: 'failed', completedMs: 13000 })).toBe(true);
    expect(page.stateLabel('passed')).toBe('Internal checks passed');
    expect(page.stateLabel('incomplete')).toBe('incomplete'); expect(page.stateLabel('invalidated')).toBe('invalidated');
  });
  it('labels error-path stored zeros conservatively while keeping both initialized and submitted values inspectable', () => {
    const { page } = workspace();
    const failed = { ...result('SAFE-ESTOP-PWM', 'failed'), completedMs: 18000, unit: 'us' };
    const initialValue = { ...failed, errorCode: -1, note: 'Factory sequence failed' };
    const submittedBeforeStopFailure = { ...failed, errorCode: 257, note: 'Method: scope CH1; Notes: zero reading' };
    for (const stored of [initialValue, submittedBeforeStopFailure, { ...failed, errorCode: undefined }]) {
      expect(page.hasStoredMeasurement(stored)).toBe(true);
      expect(page.measurementLabel(stored)).toBe('Stored value (measurement unconfirmed)');
      expect(stored.measuredValue).toBe(0);
    }
    expect(page.measurementLabel(failed)).toBe('Recorded measurement');
    expect(page.hasStoredMeasurement({ ...initialValue, state: 'running', completedMs: 0 })).toBe(false);
  });
});
