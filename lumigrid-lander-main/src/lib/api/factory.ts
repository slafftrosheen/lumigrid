import { apiJson } from './transport';

export type FactoryEffectName =
  | 'identity'
  | 'pixel_chase'
  | 'rainbow'
  | 'white_walk'
  | 'pwm_walk'
  | 'pwm_fade'
  | 'combined_pulse'
  | 'stress_cycle'
  | 'burn_in' // Wire/compatibility alias
  | 'blackout';

export type FactoryTestState =
  | 'idle'
  | 'preparing'
  | 'running'
  | 'waiting_for_operator'
  | 'passed'
  | 'failed'
  | 'aborted';

export type FactoryTestKind =
  | 'addressable_channel'
  | 'pwm_channel'
  | 'combined_outputs'
  | 'safety_gate'
  | 'burn_in';

export interface FactoryTestSession {
  id: number;
  kind: FactoryTestKind;
  state: FactoryTestState;
  channel: number; // 0..15 or -1
  startedMs?: number;
  finishedMs?: number;
  operatorConfirmationRequired?: boolean;
  errorCode?: number;
  stage?: string;
  stageIndex?: number;
  stageCount?: number;
}

export interface ChannelValidationStatus {
  channel: number;
  tested: boolean;
  passed: boolean;
  timestampMs: number;
}

export interface FactoryValidationReport {
  addressable: ChannelValidationStatus[];
  pwm: ChannelValidationStatus[];
  combined: { tested: boolean; passed: boolean; timestampMs: number };
  safetyGate: { tested: boolean; passed: boolean; timestampMs: number };
  burnIn: {
    tested: boolean;
    passed: boolean;
    timestampMs: number;
    durationS: number;
    errors: number;
  };
}

export interface FactoryStatus {
  state?: 'idle' | 'running' | 'looping' | 'waiting_for_operator';
  effect: FactoryEffectName | string;
  startedMs?: number;
  effectStartedMs: number;
  durationMs: number;
  elapsedMs?: number;
  remainingMs?: number;
  brightness: number;
  autoLoop: boolean;
  outputsEnabled: boolean;
  testActive: boolean;
  commandRejections: number;
  commitFailures?: number;
  activeChannel?: number;
  session?: FactoryTestSession | null;
}

export function getFactoryStatus(ip: string): Promise<FactoryStatus> {
  return apiJson<FactoryStatus>(ip, '/api/v1/factory/status');
}

export function postFactoryEffect(
  ip: string,
  effect: FactoryEffectName,
  brightness = 32,
  durationMs = 10000,
): Promise<{ accepted: boolean }> {
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/effect', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ effect, brightness, durationMs }),
  });
}

export function postFactoryLane(
  ip: string,
  lane: number,
  red: number,
  green: number,
  blue: number,
  brightness = 32,
  durationMs = 10000,
): Promise<{ accepted: boolean }> {
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/lane', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ lane, red, green, blue, brightness, durationMs }),
  });
}

export function postFactoryPwm(
  ip: string,
  channel: number,
  duty: number,
  durationMs = 10000,
): Promise<{ accepted: boolean }> {
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/pwm', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ channel, duty, durationMs }),
  });
}

export function postFactoryAbort(ip: string): Promise<{ accepted: boolean; action?: string }> {
  return apiJson<{ accepted: boolean; action?: string }>(ip, '/api/v1/factory/abort', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });
}

export function blackout(ip: string): Promise<{ ok: boolean }> {
  return postFactoryAbort(ip).then(() => ({ ok: true }));
}

export function postFactoryAutoLoop(ip: string, enabled: boolean): Promise<{ accepted: boolean }> {
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/auto-loop', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ enabled }),
  });
}

export function startFactoryTestSession(
  ip: string,
  kind: FactoryTestKind,
  channel = 0xFF,
  durationMs = 5000,
  brightness = 32,
): Promise<{ accepted: boolean; sessionId: number; kind: string; channel: number }> {
  return apiJson<{ accepted: boolean; sessionId: number; kind: string; channel: number }>(
    ip,
    '/api/v1/factory/test/start',
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ kind, channel, durationMs, brightness }),
    },
  );
}

export function confirmFactoryTestSession(
  ip: string,
  sessionId: number,
  passed: boolean,
  notes?: string,
): Promise<{ confirmed: boolean; sessionId: number; passed: boolean }> {
  return apiJson<{ confirmed: boolean; sessionId: number; passed: boolean }>(
    ip,
    '/api/v1/factory/test/confirm',
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ sessionId, passed, notes }),
    },
  );
}

export function abortFactoryTestSession(ip: string): Promise<{ aborted: boolean }> {
  return apiJson<{ aborted: boolean }>(ip, '/api/v1/factory/test/abort', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });
}

export function getFactoryValidationReport(ip: string): Promise<FactoryValidationReport> {
  return apiJson<FactoryValidationReport>(ip, '/api/v1/factory/test/report');
}

export function startBurnInJob(
  ip: string,
  durationMinutes = 60,
  brightness = 64,
): Promise<{ accepted: boolean; jobId: number; type: string; durationMinutes: number; brightness: number }> {
  return apiJson<{ accepted: boolean; jobId: number; type: string; durationMinutes: number; brightness: number }>(
    ip,
    '/api/v1/factory/burn-in/start',
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ durationMinutes, brightness }),
    },
  );
}

export function stopBurnInJob(ip: string): Promise<{ stopped: boolean }> {
  return apiJson<{ stopped: boolean }>(ip, '/api/v1/factory/burn-in/stop', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });
}
