/**
 * LumiGrid API client for LumiGrid Operator Dashboard.
 * All functions return typed responses; errors throw with a human-readable message.
 */

// ── Types ─────────────────────────────────────────────────────────────────────

export interface NodeInfo {
  ver:    string;
  name:   string;
  uptime: number;
  freeheap: number;
  ip:     string;
  mac:    string;
  u?: Record<string, Record<string, unknown>>;
}

export interface NodeSegment {
  id:  number;
  start: number;
  stop: number;
  len:  number;
  on:  boolean;
  bri: number;
  col: number[][];
  fx:  number;
  pal: number;
  i?:  unknown[];
}

export interface NodeState {
  on:  boolean;
  bri: number;
  transition: number;
  ps:  number;
  seg: NodeSegment[];
}

export interface ApiNode {
  name: string;
  type: number;
  ip:   string;
  vid:  number;
}

export interface ChannelConfig {
  channelLabel: string[];
  channelType:  number[];
  pwmFrequency: number;
  pcaFound:     boolean;
  tempPin:      number;
}

export interface NodeStatus {
  ip:      string;
  info:    NodeInfo | null;
  state:   NodeState | null;
  error:   string | null;
  latency: number;
}

// ── Shared transport ─────────────────────────────────────────────────────────

import {
  apiFetch,
  apiJson,
  authHeaders,
  clearApiToken,
  DEV_API_TOKEN,
  getApiToken,
  setApiToken,
} from './transport';

export {
  apiFetch,
  apiJson,
  authHeaders,
  clearApiToken,
  DEV_API_TOKEN,
  getApiToken,
  setApiToken,
};


// ── Legacy compatibility re-exports ──────────────────────────────────────────

export {
  getInfo,
  getState,
  getNodes,
  getChannelConfig,
  getSchedule,
  setState,
  applyPreset,
  setBrightness,
  setPower,
  emergencyOff,
  setPixelColor,
  setPixels,
  uploadFile,
  saveSchedule,
  uploadFirmware,
  getNodeStatus,
} from './legacy';

// ── Scene / Preset types & API ────────────────────────────────────────────────

export interface RgbColor {
  r: number;
  g: number;
  b: number;
}

export interface SceneLayer {
  effect: string;
  blendMode: string;
  primaryColor: RgbColor;
  secondaryColor: RgbColor;
  speedMs: number;
  fxParam: number;
  paletteId: number;
  opacity: number;
  enabled: boolean;
}

export interface SceneZone {
  layers: [SceneLayer, SceneLayer, SceneLayer];
}

export interface ScenePreset {
  index: number;
  name: string;
  globalBrightness: number;
  transitionMs: number;
  zones: SceneZone[];
}

export interface EffectMeta {
  id: string;
  label: string;
  description: string;
  hasParameter: boolean;
  parameterLabel?: string;
  parameterUnit?: string;
  parameterMin?: number;
  parameterMax?: number;
  parameterDefault?: number;
}

export function getPresets(ip: string): Promise<ScenePreset[]> {
  return apiJson<ScenePreset[]>(ip, '/api/v1/presets');
}

export function savePreset(ip: string, preset: Partial<ScenePreset> & { index: number }): Promise<{ saved: true }> {
  return apiJson<{ saved: true }>(ip, '/api/v1/presets', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(preset),
  });
}

export function activatePreset(ip: string, index: number, durationMs = 60000): Promise<{ activated: true }> {
  return apiJson<{ activated: true }>(ip, '/api/v1/activate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ preset: index, durationMs }),
  });
}

export function getEffects(ip: string): Promise<EffectMeta[]> {
  return apiJson<EffectMeta[]>(ip, '/api/v1/effects');
}

// ── Sequence / Timeline types & API ────────────────────────────────────────────

export interface SequenceStep {
  presetIndex: number;
  lengthBeats: number;
}

export interface SequenceTrack {
  channelId: number;
  numSteps: number;
  enabled: boolean;
  steps: SequenceStep[];
}

export interface Sequence {
  bpm: number;
  enabled: boolean;
  tracks: SequenceTrack[];
}

export function getSequence(ip: string): Promise<Sequence> {
  return apiJson<Sequence>(ip, '/api/v1/sequence');
}

export function saveSequence(ip: string, seq: Sequence): Promise<{ saved: true }> {
  return apiJson<{ saved: true }>(ip, '/api/v1/sequence', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(seq),
  });
}

// ── Calendar Schedule types & API ──────────────────────────────────────────────

export type CalendarAction = 'preset' | 'sequence';

export interface CalendarEntry {
  index: number;
  hour: number;
  minute: number;
  daysOfWeek: number;
  action: CalendarAction;
  presetIndex: number;
  enabled: boolean;
}

export interface CalendarSchedule {
  entries: CalendarEntry[];
}

export function getCalendarSchedule(ip: string): Promise<CalendarSchedule> {
  return apiJson<CalendarSchedule>(ip, '/api/v1/schedules');
}

export function saveCalendarSchedule(ip: string, schedule: CalendarSchedule): Promise<{ saved: true }> {
  return apiJson<{ saved: true }>(ip, '/api/v1/schedules', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(schedule),
  });
}

// ── Schedule types (shared) ────────────────────────────────────────────────────

export interface AnimationBlock {
  id:                string;
  file:              string;
  start_time:        string; // "HH:MM"
  duration_minutes:  number;
  fallback_preset:   number;
}

// ── Health / Diagnostics types & API ───────────────────────────────────────────

export interface HealthSystem {
  uptime_s: number;
  free_heap: number;
  min_free_heap: number;
  reset_reason: number;
  fps: number;
}

export interface HealthNetwork {
  wifi_rssi: number;
}

export interface HealthNvs {
  used: number;
  free: number;
}

export interface HealthConfig {
  valid: boolean;
  error_count: number;
  first_error: string;
  persistence_state: string;
  primary_valid: boolean;
  backup_valid: boolean;
}

export interface HealthStartup {
  state: string;
  stage: string;
  failing_stage: string;
  failing_error: number;
}

export interface HealthOutput {
  state: string;
  fault: boolean;
  fault_events: number;
  failed_commits: number;
  last_error: number;
  estop_latched: boolean;
  runtime_estop: boolean;
  control_source: number;
  live_objects: number;
  current_power_mw: number;
  throttling_events: number;
  active_preset: number;
  watchdog_fallback: boolean;
}

export interface HealthDmx {
  sources_active: number;
  universes_active: number;
  sources_dropped: number;
  packets_accepted: number;
  packets_rejected_seq: number;
  packets_ignored_unpatched: number;
  submission_failures: number;
  configured_universes: number[];
}

export interface HealthSync {
  packet_loss: number;
}

export interface HealthSnapshot {
  schema_version: number;
  system: HealthSystem;
  network: HealthNetwork;
  nvs: HealthNvs;
  config: HealthConfig;
  startup: HealthStartup;
  output: HealthOutput;
  dmx: HealthDmx;
  sync: HealthSync;
}

export interface AuditEvent {
  timestamp_s: number;
  role: number;
  action: string;
  detail: string;
}

export function getHealth(ip: string): Promise<HealthSnapshot> {
  return apiJson<HealthSnapshot>(ip, '/api/v1/health');
}

export function getLogs(ip: string): Promise<string> {
  return apiFetch(ip, '/api/v1/logs').then(async res => {
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.text();
  });
}

export function getAuditLog(ip: string): Promise<AuditEvent[]> {
  return apiJson<AuditEvent[]>(ip, '/api/v1/audit');
}

// ── Factory / Burn-in types & API ──────────────────────────────────────────────

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
  effect: FactoryEffectName;
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
  brightness?: number,
  durationMs?: number,
): Promise<{ accepted: boolean }> {
  const body: Record<string, unknown> = { effect };
  if (brightness !== undefined) body.brightness = brightness;
  if (durationMs !== undefined) body.durationMs = durationMs;
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/effect', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
}

export function postFactoryLane(
  ip: string,
  lane: number,
  red: number,
  green: number,
  blue: number,
  brightness?: number,
  durationMs?: number,
): Promise<{ accepted: boolean }> {
  const body: Record<string, unknown> = { lane, red, green, blue };
  if (brightness !== undefined) body.brightness = brightness;
  if (durationMs !== undefined) body.durationMs = durationMs;
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/lane', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
}

export function postFactoryPwm(
  ip: string,
  channel: number,
  duty: number,
  durationMs?: number,
): Promise<{ accepted: boolean }> {
  const body: Record<string, unknown> = { channel, duty };
  if (durationMs !== undefined) body.durationMs = durationMs;
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/pwm', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
}

export function postFactoryAbort(ip: string): Promise<{ accepted: boolean; action: string }> {
  return apiJson<{ accepted: boolean; action: string }>(ip, '/api/v1/factory/abort', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });
}

export function postFactoryAutoLoop(ip: string, enabled: boolean): Promise<{ accepted: boolean }> {
  return apiJson<{ accepted: boolean }>(ip, '/api/v1/factory/auto-loop', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ enabled }),
  });
}

// ── Device Configuration & Recovery types & API ────────────────────────────────

export interface DeviceProtocolConfig {
  artnetEnabled: boolean;
  sacnEnabled: boolean;
  universeStart: number;
  liveStreamLayer: number;
}

export interface DeviceAuthConfig {
  adminToken?: string;
  installerToken?: string;
  operatorToken?: string;
}

export interface DeviceZoneConfig {
  name: string;
  role: string;
}

export interface DeviceConfig {
  deviceName: string;
  activePresetIndex: number;
  fallbackPresetIndex: number;
  watchdogTimeoutS: number;
  maxPowerMw: number;
  gammaX10: number;
  softStartEnabled: boolean;
  softStartMs: number;
  mqttEnabled: boolean;
  mqttBroker: string;
  mqttPort: number;
  syncEnabled: boolean;
  syncRole: number;
  protocol: DeviceProtocolConfig;
  auth?: DeviceAuthConfig;
  zones?: DeviceZoneConfig[];
}

export function getDeviceConfig(ip: string): Promise<DeviceConfig> {
  return apiJson<DeviceConfig>(ip, '/api/v1/config');
}

export function saveDeviceConfig(ip: string, config: DeviceConfig): Promise<{ saved: true }> {
  const sanitized = { ...config };
  if (sanitized.auth) {
    const authCopy: DeviceAuthConfig = {};
    if (sanitized.auth.adminToken?.trim()) authCopy.adminToken = sanitized.auth.adminToken.trim();
    if (sanitized.auth.installerToken?.trim()) authCopy.installerToken = sanitized.auth.installerToken.trim();
    if (sanitized.auth.operatorToken?.trim()) authCopy.operatorToken = sanitized.auth.operatorToken.trim();
    if (Object.keys(authCopy).length > 0) {
      sanitized.auth = authCopy;
    } else {
      delete sanitized.auth;
    }
  }
  return apiJson<{ saved: true }>(ip, '/api/v1/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(sanitized),
  });
}

export function getBackup(ip: string): Promise<Record<string, unknown>> {
  return apiJson<Record<string, unknown>>(ip, '/api/v1/backup');
}

export function restoreBackup(ip: string, payload: Record<string, unknown>): Promise<{ restored: true }> {
  return apiJson<{ restored: true }>(ip, '/api/v1/restore', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
}

