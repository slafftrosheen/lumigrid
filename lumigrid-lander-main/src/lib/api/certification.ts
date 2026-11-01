import { apiJson } from './transport';

export type CertificationState =
  | 'not_started'
  | 'in_progress'
  | 'passed'
  | 'failed'
  | 'incomplete'
  | 'invalidated';

export type CertificationTestState =
  | 'pending'
  | 'running'
  | 'waiting_for_operator'
  | 'passed'
  | 'failed'
  | 'skipped'
  | 'blocked';

export type EvidenceSource =
  | 'automatic'
  | 'operator_observation'
  | 'instrument_measurement';

export type RequirementLevel = 'required' | 'conditional' | 'optional';

export type CertificationProfile =
  | 'development'
  | 'production_unit'
  | 'release_candidate';

export interface CertificationTestDefinition {
  id: string;
  category: string;
  title: string;
  requiredEvidence: EvidenceSource;
  reqDev: RequirementLevel;
  reqProd: RequirementLevel;
  reqRc: RequirementLevel;
  timeoutMs: number;
  expectedValue: number;
  tolerance: number;
  unit: string;
}

export interface CertificationPlanResponse {
  schema: number;
  planVersion: number;
  tests: CertificationTestDefinition[];
}

export interface CertificationTestResultItem {
  id: string;
  state: CertificationTestState;
  evidenceSource: EvidenceSource;
  startedMs: number;
  completedMs: number;
  errorCode: number;
  measuredValue: number;
  unit: string;
  note: string;
  attempts: number;
}

export interface CertificationStatusResponse {
  schema: number;
  certificationId: string;
  profile: CertificationProfile;
  planVersion: number;
  state: CertificationState;
  nodeId: string;
  firmwareVersion: string;
  hardwareRevision: string;
  serialNumber: string;
  passed: number;
  failed: number;
  skipped: number;
  total: number;
  pending: number;
  stale: boolean;
  staleReason?: string;
  currentTest?: string;
  /** Absent on older firmware: its evidence endpoints are read-only in this UI. */
  evidenceBinding?: 'session_attempt_v1' | string;
  cancellationPending?: boolean;
  confirmationPending?: boolean;
  tests: CertificationTestResultItem[];
}

export interface BurnInReportMetrics {
  durationS: number;
  commitFailures: number;
  outputFaults: number;
  startFreeHeap: number;
  minFreeHeap: number;
  endFreeHeap: number;
  startFree8?: number;
  minFree8?: number;
  endFree8?: number;
  startLargest8?: number;
  minLargest8?: number;
  endLargest8?: number;
  networkDisconnects: number;
  watchdogResets: number;
}

export interface CertificationReportResponse {
  schema: string;
  certificationId: string;
  nodeId: string;
  serialNumber: string;
  firmwareVersion: string;
  /** Identifies the image exporting this report, not necessarily the tested image. */
  reportingFirmwareVersion?: string;
  reportingFirmwareElfSha256?: string;
  hardwareRevision: string;
  projectName: string;
  planVersion: number;
  profile: CertificationProfile;
  state: CertificationState;
  stale: boolean;
  staleReason?: string;
  startedMs: number;
  completedMs: number;
  passedCount: number;
  failedCount: number;
  skippedCount: number;
  testCount: number;
  configFingerprint: number;
  tests: Array<CertificationTestResultItem & { category?: string; title?: string; requirement?: string }>;
  burnIn: BurnInReportMetrics;
}

export async function getCertificationPlan(
  ip: string,
  apiKey?: string,
  timeoutMs = 5000
): Promise<CertificationPlanResponse> {
  return apiJson<CertificationPlanResponse>(ip, '/api/v1/certification/plan', { apiKey }, timeoutMs);
}

export async function getCertificationStatus(
  ip: string,
  apiKey?: string,
  timeoutMs = 5000
): Promise<CertificationStatusResponse> {
  return apiJson<CertificationStatusResponse>(ip, '/api/v1/certification/status', { apiKey }, timeoutMs);
}

export async function startCertification(
  ip: string,
  profile: CertificationProfile,
  serialNumber: string,
  certificationId: string,
  apiKey?: string
): Promise<{ certificationId: string; planVersion: number; profile: string; state: string }> {
  return apiJson(ip, '/api/v1/certification/start', {
    apiKey,
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ profile, serialNumber, certificationId }),
  });
}

export async function startCertificationTest(
  ip: string,
  testId: string,
  certificationId: string,
  apiKey?: string
): Promise<{ status: string }> {
  return apiJson(ip, '/api/v1/certification/test/start', {
    apiKey,
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ testId, certificationId }),
  });
}

export async function submitCertificationTestResult(
  ip: string,
  data: {
    testId: string;
    certificationId: string;
    testStartedMs: number;
    testAttempt: number;
    passed: boolean;
    state?: CertificationTestState;
    evidenceSource?: EvidenceSource;
    measuredValue?: number;
    unit?: string;
    note?: string;
  },
  apiKey?: string
): Promise<{ status: string; overallState: string }> {
  return apiJson(ip, '/api/v1/certification/test/result', {
    apiKey,
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data),
  });
}

export async function abortCertification(
  ip: string,
  certificationId: string,
  apiKey?: string
): Promise<{ status: string; accepted?: boolean; cancellationRequested?: boolean }> {
  return apiJson(ip, '/api/v1/certification/abort', {
    apiKey,
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ certificationId }),
  });
}

export async function getCertificationReport(
  ip: string,
  apiKey?: string,
  timeoutMs = 5000
): Promise<CertificationReportResponse> {
  return apiJson<CertificationReportResponse>(ip, '/api/v1/certification/report', { apiKey }, timeoutMs);
}
