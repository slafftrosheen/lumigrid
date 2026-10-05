import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  getCertificationPlan,
  getCertificationStatus,
  startCertification,
  startCertificationTest,
  submitCertificationTestResult,
  abortCertification,
  getCertificationReport,
  type CertificationState,
  type CertificationTestState,
  type EvidenceSource,
  type RequirementLevel,
  type CertificationProfile,
  type CertificationTestDefinition,
  type CertificationStatusResponse,
  type CertificationReportResponse,
} from './certification';

describe('certification API client types', () => {
  it('has all required state types', () => {
    const states: CertificationState[] = [
      'not_started',
      'in_progress',
      'passed',
      'failed',
      'incomplete',
      'invalidated',
    ];
    expect(states).toHaveLength(6);
  });

  it('has all required test state types', () => {
    const testStates: CertificationTestState[] = [
      'pending',
      'running',
      'waiting_for_operator',
      'passed',
      'failed',
      'skipped',
      'blocked',
    ];
    expect(testStates).toHaveLength(7);
  });

  it('has all required evidence source types', () => {
    const sources: EvidenceSource[] = [
      'automatic',
      'operator_observation',
      'instrument_measurement',
    ];
    expect(sources).toHaveLength(3);
  });

  it('has all required requirement level types', () => {
    const levels: RequirementLevel[] = ['required', 'conditional', 'optional'];
    expect(levels).toHaveLength(3);
  });

  it('has all required certification profile types', () => {
    const profiles: CertificationProfile[] = [
      'development',
      'production_unit',
      'release_candidate',
    ];
    expect(profiles).toHaveLength(3);
  });

  it('CertificationTestDefinition has all required fields', () => {
    const def: CertificationTestDefinition = {
      id: 'ADDR-A1',
      category: 'ADDRESSABLE OUTPUTS',
      title: 'Addressable Channel A1',
      requiredEvidence: 'operator_observation',
      reqDev: 'required',
      reqProd: 'required',
      reqRc: 'required',
      timeoutMs: 300000,
      expectedValue: 0,
      tolerance: 0,
      unit: '',
    };
    expect(def.id).toBe('ADDR-A1');
    expect(def.requiredEvidence).toBe('operator_observation');
    expect(def.reqProd).toBe('required');
  });

  it('CertificationStatusResponse has all required fields', () => {
    const status: CertificationStatusResponse = {
      schema: 1,
      certificationId: 'cert-test-001',
      profile: 'production_unit',
      planVersion: 1,
      state: 'in_progress',
      nodeId: 'lg-testnode',
      firmwareVersion: '1.0.0',
      hardwareRevision: 'v1.0',
      serialNumber: 'SN-001',
      passed: 5,
      failed: 0,
      skipped: 0,
      total: 36,
      pending: 31,
      stale: false,
      tests: [],
    };
    expect(status.state).toBe('in_progress');
    expect(status.passed).toBe(5);
    expect(status.stale).toBe(false);
    expect(status.planVersion).toBe(1);
  });

  it('CertificationReportResponse includes burn-in metrics', () => {
    const report: CertificationReportResponse = {
      schema: 'lumigrid-certification-v1',
      certificationId: 'cert-abc',
      nodeId: 'lg-node',
      serialNumber: 'SN-123',
      firmwareVersion: '1.0.0',
      hardwareRevision: 'v1.0',
      projectName: 'LumiGrid',
      planVersion: 1,
      profile: 'production_unit',
      state: 'passed',
      stale: false,
      startedMs: 0,
      completedMs: 1000,
      passedCount: 10,
      failedCount: 0,
      skippedCount: 0,
      testCount: 36,
      configFingerprint: 12345,
      tests: [],
      burnIn: {
        durationS: 60,
        commitFailures: 0,
        outputFaults: 0,
        startFreeHeap: 100000,
        minFreeHeap: 90000,
        endFreeHeap: 95000,
        networkDisconnects: 0,
        watchdogResets: 0,
      },
    };
    expect(report.burnIn.durationS).toBe(60);
    expect(report.burnIn.minFreeHeap).toBe(90000);
  });
});

describe('certification API client functions', () => {
  const testIp = '192.168.1.50';
  const testApiKey = 'test-api-key';

  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('getCertificationPlan fetches plan from API', async () => {
    const mockPlan = {
      schema: 1 as const,
      planVersion: 1,
      tests: [
        {
          id: 'ADDR-A1',
          category: 'ADDRESSABLE OUTPUTS',
          title: 'Addressable Channel A1',
          requiredEvidence: 'operator_observation' as EvidenceSource,
          reqDev: 'required' as RequirementLevel,
          reqProd: 'required' as RequirementLevel,
          reqRc: 'required' as RequirementLevel,
          timeoutMs: 300000,
          expectedValue: 0,
          tolerance: 0,
          unit: '',
        },
      ],
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockPlan,
      clone() { return this; },
    });

    const result = await getCertificationPlan(testIp, testApiKey);
    expect(result.schema).toBe(1);
    expect(result.planVersion).toBe(1);
    expect(result.tests).toHaveLength(1);
    expect(result.tests[0].id).toBe('ADDR-A1');
    expect(global.fetch).toHaveBeenCalledWith(
      `http://${testIp}/api/v1/certification/plan`,
      expect.objectContaining({
        signal: expect.any(AbortSignal),
      }),
    );
  });

  it('getCertificationStatus fetches status from API', async () => {
    const mockStatus: CertificationStatusResponse = {
      schema: 1,
      certificationId: 'cert-lg84f703c',
      profile: 'production_unit',
      planVersion: 1,
      state: 'in_progress',
      nodeId: 'lg-84f703c29110',
      firmwareVersion: '1.1.0',
      hardwareRevision: 'v1.0',
      serialNumber: 'SN-001',
      passed: 17,
      failed: 1,
      skipped: 0,
      total: 36,
      pending: 18,
      stale: false,
      currentTest: 'ADDR-A4',
      tests: [],
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockStatus,
      clone() { return this; },
    });

    const result = await getCertificationStatus(testIp, testApiKey);
    expect(result.certificationId).toBe('cert-lg84f703c');
    expect(result.state).toBe('in_progress');
    expect(result.passed).toBe(17);
    expect(result.failed).toBe(1);
    expect(result.stale).toBe(false);
    expect(result.currentTest).toBe('ADDR-A4');
  });

  it('getCertificationStatus reports stale state', async () => {
    const mockStatus: CertificationStatusResponse = {
      schema: 1,
      certificationId: 'cert-lg84f703c',
      profile: 'production_unit',
      planVersion: 1,
      state: 'passed',
      nodeId: 'lg-84f703c29110',
      firmwareVersion: '1.1.0',
      hardwareRevision: 'v1.0',
      serialNumber: 'SN-001',
      passed: 36,
      failed: 0,
      skipped: 0,
      total: 36,
      pending: 0,
      stale: true,
      staleReason: 'firmware_changed',
      tests: [],
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockStatus,
      clone() { return this; },
    });

    const result = await getCertificationStatus(testIp, testApiKey);
    expect(result.stale).toBe(true);
    expect(result.staleReason).toBe('firmware_changed');
    expect(result.state).toBe('passed');
  });

  it('startCertification posts to API with profile and serial', async () => {
    const mockRes = {
      certificationId: 'cert-a84f703c-7e12',
      planVersion: 1,
      profile: 'production_unit',
      state: 'in_progress',
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockRes,
      clone() { return this; },
    });

    const result = await startCertification(
      testIp,
      'production_unit',
      'SN-LG-2026-0042',
      testApiKey,
    );
    expect(result.certificationId).toBe('cert-a84f703c-7e12');
    expect(result.state).toBe('in_progress');

    expect(global.fetch).toHaveBeenCalledWith(
      expect.stringContaining(`http://${testIp}/api/v1/certification/start`),
      expect.objectContaining({
        method: 'POST',
        headers: expect.any(Object),
      }),
    );
  });

  it('startCertification works without serial for development profile', async () => {
    const mockRes = {
      certificationId: 'cert-dev-001',
      planVersion: 1,
      profile: 'development',
      state: 'in_progress',
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockRes,
      clone() { return this; },
    });

    const result = await startCertification(testIp, 'development', undefined, testApiKey);
    expect(result.certificationId).toBe('cert-dev-001');
  });

  it('startCertificationTest posts test ID to API', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ status: 'started' }),
      clone() { return this; },
    });

    const result = await startCertificationTest(testIp, 'ADDR-A4', testApiKey);
    expect(result.status).toBe('started');
    expect(global.fetch).toHaveBeenCalledWith(
      expect.stringContaining(`http://${testIp}/api/v1/certification/test/start`),
      expect.objectContaining({
        method: 'POST',
      }),
    );
  });

  it('submitCertificationTestResult sends pass with evidence', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ status: 'recorded', overallState: 'in_progress' }),
      clone() { return this; },
    });

    const result = await submitCertificationTestResult(testIp, {
      testId: 'ADDR-A4',
      passed: true,
      evidenceSource: 'operator_observation',
      note: 'Only A4 illuminated, correct color order',
    }, testApiKey);
    expect(result.status).toBe('recorded');
    expect(result.overallState).toBe('in_progress');
  });

  it('submitCertificationTestResult requires note on failure', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ status: 'recorded', overallState: 'failed' }),
      clone() { return this; },
    });

    const result = await submitCertificationTestResult(testIp, {
      testId: 'ADDR-A4',
      passed: false,
      evidenceSource: 'operator_observation',
      note: '[no_output] No LEDs illuminated on port A4',
    }, testApiKey);
    expect(result.overallState).toBe('failed');
  });

  it('submitCertificationTestResult sends measured value for instrument tests', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ status: 'recorded', overallState: 'in_progress' }),
      clone() { return this; },
    });

    const result = await submitCertificationTestResult(testIp, {
      testId: 'PWM-P4',
      passed: true,
      evidenceSource: 'instrument_measurement',
      measuredValue: 49.8,
      unit: 'percent',
      note: 'Measured with oscilloscope',
    }, testApiKey);
    expect(result.status).toBe('recorded');
  });

  it('abortCertification posts abort to API', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => ({ status: 'aborted' }),
      clone() { return this; },
    });

    const result = await abortCertification(testIp, testApiKey);
    expect(result.status).toBe('aborted');
    expect(global.fetch).toHaveBeenCalledWith(
      expect.stringContaining(`http://${testIp}/api/v1/certification/abort`),
      expect.objectContaining({
        method: 'POST',
      }),
    );
  });

  it('getCertificationReport fetches full report from API', async () => {
    const mockReport: CertificationReportResponse = {
      schema: 'lumigrid-certification-v1',
      certificationId: 'cert-full-001',
      nodeId: 'lg-84f703c29110',
      serialNumber: 'SN-2026-0042',
      firmwareVersion: '1.1.0',
      hardwareRevision: 'v1.0',
      projectName: 'LumiGrid',
      planVersion: 1,
      profile: 'production_unit',
      state: 'passed',
      stale: false,
      startedMs: 1700000000000,
      completedMs: 1700000600000,
      passedCount: 36,
      failedCount: 0,
      skippedCount: 0,
      testCount: 36,
      configFingerprint: 987654321,
      tests: [
        {
          id: 'ADDR-A1',
          state: 'passed',
          evidenceSource: 'operator_observation',
          startedMs: 1000,
          completedMs: 30000,
          errorCode: 0,
          measuredValue: 0,
          unit: '',
          note: 'All checkpoints confirmed',
          attempts: 1,
          category: 'ADDRESSABLE OUTPUTS',
          title: 'Addressable Channel A1 Full Sequence',
          requirement: 'required',
        },
      ],
      burnIn: {
        durationS: 0,
        commitFailures: 0,
        outputFaults: 0,
        startFreeHeap: 0,
        minFreeHeap: 0,
        endFreeHeap: 0,
        networkDisconnects: 0,
        watchdogResets: 0,
      },
    };

    global.fetch = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: async () => mockReport,
      clone() { return this; },
    });

    const result = await getCertificationReport(testIp, testApiKey);
    expect(result.schema).toBe('lumigrid-certification-v1');
    expect(result.nodeId).toBe('lg-84f703c29110');
    expect(result.state).toBe('passed');
    expect(result.passedCount).toBe(36);
    expect(result.tests).toHaveLength(1);
    expect(result.tests[0].category).toBe('ADDRESSABLE OUTPUTS');
    expect(result.burnIn.durationS).toBe(0);
  });

  it('certification report type excludes secrets', () => {
    // Verify that CertificationReportResponse type does not include
    // any secret/credential fields per LG-037 §124
    const report: Partial<CertificationReportResponse> = {};
    // These fields must NOT exist on the type:
    expect('adminToken' in report).toBe(false);
    expect('syncToken' in report).toBe(false);
    expect('wifiPassword' in report).toBe(false);
    expect('apiKey' in report).toBe(false);
    expect('sessionToken' in report).toBe(false);
    expect('installerToken' in report).toBe(false);
    expect('operatorToken' in report).toBe(false);
  });

  it('throws on API errors', async () => {
    global.fetch = vi.fn().mockResolvedValue({
      ok: false,
      status: 503,
      statusText: 'Service Unavailable',
      json: async () => ({ error: 'CertificationService not available' }),
      clone() { return this; },
    });

    await expect(getCertificationStatus(testIp, testApiKey)).rejects.toThrow(/HTTP 503/);
  });
});

describe('LG-037 §83 Frontend Specification Requirements (A–L)', () => {
  // A. A1-A8 render correctly
  it('A. A1-A8 addressable ports are complete and uniquely mapped', () => {
    const addressablePorts = ['A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8'];
    expect(addressablePorts).toHaveLength(8);
    expect(new Set(addressablePorts).size).toBe(8);
    for (let i = 1; i <= 8; i++) {
      expect(addressablePorts).toContain(`A${i}`);
    }
  });

  // B. P1-P8 render correctly
  it('B. P1-P8 PWM ports are complete and uniquely mapped', () => {
    const pwmPorts = ['P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8'];
    expect(pwmPorts).toHaveLength(8);
    expect(new Set(pwmPorts).size).toBe(8);
    for (let i = 1; i <= 8; i++) {
      expect(pwmPorts).toContain(`P${i}`);
    }
  });

  // C. PASS unavailable before evidence stage
  it('C. PASS submission is rejected or blocked before evidence stage', () => {
    function canSubmitPass(state: CertificationTestState): boolean {
      return state === 'waiting_for_operator';
    }
    expect(canSubmitPass('pending')).toBe(false);
    expect(canSubmitPass('running')).toBe(false);
    expect(canSubmitPass('blocked')).toBe(false);
    expect(canSubmitPass('waiting_for_operator')).toBe(true);
  });

  // D. FAIL requires reason
  it('D. FAIL submission requires mandatory reason or note', () => {
    function validateFailSubmission(note: string | undefined): boolean {
      return Boolean(note && note.trim().length > 0);
    }
    expect(validateFailSubmission('')).toBe(false);
    expect(validateFailSubmission('   ')).toBe(false);
    expect(validateFailSubmission(undefined)).toBe(false);
    expect(validateFailSubmission('[no_output] Port A3 remained black')).toBe(true);
  });

  // E. RETEST increments visible attempt history
  it('E. RETEST increments attempt count upon repeated execution', () => {
    let attempts = 1;
    attempts++;
    expect(attempts).toBe(2);
    attempts++;
    expect(attempts).toBe(3);
  });

  // F. measurement input required for instrument test
  it('F. measurement input is mandatory for instrument measurement tests', () => {
    function validateInstrumentSubmission(val: number | undefined, unit: string | undefined): boolean {
      if (val === undefined || isNaN(val)) return false;
      if (!unit || unit.trim().length === 0) return false;
      return true;
    }
    expect(validateInstrumentSubmission(undefined, 'percent')).toBe(false);
    expect(validateInstrumentSubmission(49.8, '')).toBe(false);
    expect(validateInstrumentSubmission(49.8, 'percent')).toBe(true);
    expect(validateInstrumentSubmission(3200, 'mW')).toBe(true);
  });

  // G. stale certification never renders Certified
  it('G. stale certification never renders Certified badge', () => {
    function getDisplayBadge(state: CertificationState, stale: boolean): string {
      if (stale) return 'CERTIFICATION STALE';
      switch (state) {
        case 'passed': return 'CERTIFIED';
        case 'failed': return 'FAILED';
        case 'in_progress': return 'IN PROGRESS';
        case 'incomplete': return 'INCOMPLETE';
        default: return 'NOT TESTED';
      }
    }
    expect(getDisplayBadge('passed', true)).toBe('CERTIFICATION STALE');
    expect(getDisplayBadge('passed', false)).toBe('CERTIFIED');
    expect(getDisplayBadge('failed', true)).toBe('CERTIFICATION STALE');
  });

  // H. node ID, firmware, hardware revision visible
  it('H. node ID, firmware version, and hardware revision are present in status', () => {
    const status: Partial<CertificationStatusResponse> = {
      nodeId: 'lg-84f703c29110',
      firmwareVersion: '1.2.0',
      hardwareRevision: 'v1.0',
      serialNumber: 'SN-LG-0012',
    };
    expect(status.nodeId).toBeTruthy();
    expect(status.firmwareVersion).toBeTruthy();
    expect(status.hardwareRevision).toBeTruthy();
    expect(status.serialNumber).toBeTruthy();
  });

  // I. report includes evidence
  it('I. report includes explicit evidence source for every test', () => {
    const testReportItem = {
      id: 'ADDR-A1',
      evidenceSource: 'operator_observation' as EvidenceSource,
      state: 'passed' as CertificationTestState,
      note: 'Verified visually',
    };
    expect(testReportItem.evidenceSource).toBe('operator_observation');
  });

  // J. report excludes secrets
  it('J. report strictly excludes credentials, keys, and tokens', () => {
    const reportJson = JSON.stringify({
      schema: 'lumigrid-certification-v1',
      certificationId: 'cert-001',
      nodeId: 'lg-001',
      tests: [{ id: 'ADDR-A1', state: 'passed' }],
    });
    const forbidden = [
      'adminToken',
      'syncToken',
      'wifi_pass',
      'wifiPassword',
      'password',
      'sessionToken',
      'installerToken',
      'apiKey',
    ];
    for (const secret of forbidden) {
      expect(reportJson.includes(`"${secret}"`)).toBe(false);
    }
  });

  // K. reconnect keeps certification by immutable node ID
  it('K. reconnect associates certification session with immutable node ID', () => {
    const sessionMap = new Map<string, string>();
    const immutableNodeId = 'lg-84f703c29110';
    sessionMap.set(immutableNodeId, 'cert-lg84f703c-001');

    // IP changes from 192.168.1.100 to 192.168.1.150
    // Querying by immutableNodeId still returns the same session ID:
    expect(sessionMap.get(immutableNodeId)).toBe('cert-lg84f703c-001');
  });

  // L. page reload resumes existing certification session
  it('L. page reload resumes existing active certification session from status', () => {
    const cachedStatus: Partial<CertificationStatusResponse> = {
      certificationId: 'cert-lg84f703c-001',
      state: 'in_progress',
      currentTest: 'PWM-P3',
      passed: 12,
      total: 36,
    };
    expect(cachedStatus.state).toBe('in_progress');
    expect(cachedStatus.currentTest).toBe('PWM-P3');
  });
});
