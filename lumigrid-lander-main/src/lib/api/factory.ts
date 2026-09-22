import { apiJson } from './transport';

export type FactoryEffectName =
  | 'identity'
  | 'pixel_chase'
  | 'rainbow'
  | 'white_walk'
  | 'pwm_walk'
  | 'pwm_fade'
  | 'combined_pulse'
  | 'burn_in'
  | 'blackout';

export interface FactoryStatus {
  effect: string;
  effectStartedMs: number;
  durationMs: number;
  brightness: number;
  autoLoop: boolean;
  outputsEnabled: boolean;
  testActive: boolean;
  commandRejections: number;
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

export function postFactoryAbort(ip: string): Promise<{ accepted: boolean; action: string }> {
  return apiJson<{ accepted: boolean; action: string }>(ip, '/api/v1/factory/abort', {
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
