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

// ── Core fetch helper ─────────────────────────────────────────────────────────

const API_TOKEN_KEY = 'lg_api_token';
// Matches ConfigStore's development/default admin credential.
const DEV_API_TOKEN = 'lumigrid_admin';

/**
 * Returns the node Bearer token used by the operator console.
 * In development the firmware's default admin credential is used. A real
 * configured token can be supplied through localStorage without changing the
 * client code.
 */
export function getApiToken(): string {
  if (typeof localStorage === 'undefined') return DEV_API_TOKEN;
  return localStorage.getItem(API_TOKEN_KEY) ?? DEV_API_TOKEN;
}

export function setApiToken(token: string): void {
  if (typeof localStorage !== 'undefined') {
    const value = token.trim();
    if (value) localStorage.setItem(API_TOKEN_KEY, value);
    else localStorage.removeItem(API_TOKEN_KEY);
  }
}

export function clearApiToken(): void {
  if (typeof localStorage !== 'undefined') localStorage.removeItem(API_TOKEN_KEY);
}

function authHeaders(extra?: HeadersInit): Headers {
  const headers = new Headers(extra);
  const token = getApiToken();
  if (token) headers.set('Authorization', `Bearer ${token}`);
  return headers;
}

async function wfetch<T>(ip: string, path: string, opts?: RequestInit, timeout = 8000): Promise<T> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeout);
  const url = `http://${ip}${path}`;
  try {
    const res = await fetch(url, {
      ...opts,
      headers: authHeaders(opts?.headers),
      signal: ctrl.signal,
    });
    if (!res.ok) {
      let detail = '';
      try {
        const body = await res.clone().json() as { error?: string };
        detail = body?.error ? `: ${body.error}` : '';
      } catch {
        // Keep the HTTP status as the useful fallback for non-JSON responses.
      }
      throw new Error(`HTTP ${res.status} ${res.statusText}${detail}`);
    }
    return (await res.json()) as T;
  } catch (e: unknown) {
    const msg = e instanceof Error ? e.message : String(e);
    throw new Error(`[${ip}] ${path} → ${msg}`);
  } finally {
    clearTimeout(timer);
  }
}

async function rawFetch(ip: string, path: string, opts?: RequestInit, timeout = 8000): Promise<Response> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeout);
  try {
    return await fetch(`http://${ip}${path}`, {
      ...opts,
      headers: authHeaders(opts?.headers),
      signal: ctrl.signal,
    });
  } finally {
    clearTimeout(timer);
  }
}

// ── Read endpoints ─────────────────────────────────────────────────────────────

export function getInfo(ip: string): Promise<NodeInfo> {
  return wfetch<NodeInfo>(ip, '/json/info');
}

export function getState(ip: string): Promise<NodeState> {
  return wfetch<NodeState>(ip, '/json/state');
}

export function getNodes(ip: string): Promise<{ nodes: ApiNode[] }> {
  return wfetch<{ nodes: ApiNode[] }>(ip, '/json/nodes');
}

export async function getChannelConfig(ip: string): Promise<ChannelConfig | null> {
  try {
    const cfg = await wfetch<{ um?: { Lumigrid_PCA9685?: ChannelConfig } }>(ip, '/json/cfg');
    return cfg?.um?.Lumigrid_PCA9685 ?? null;
  } catch {
    return null;
  }
}

/** Read /animations.json from LED Node Storage. Returns null if not found. */
export async function getSchedule(ip: string): Promise<AnimationBlock[] | null> {
  try {
    const res = await rawFetch(ip, '/animations.json');
    if (!res.ok) return null;
    const j = await res.json() as { animations?: AnimationBlock[] };
    return j.animations ?? [];
  } catch {
    return null;
  }
}

// ── Write endpoints ────────────────────────────────────────────────────────────

export function setState(ip: string, state: Partial<NodeState> | Record<string, unknown>): Promise<NodeState> {
  return wfetch<NodeState>(ip, '/json/state', {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(state)
  });
}

export function applyPreset(ip: string, presetId: number): Promise<NodeState> {
  return setState(ip, { ps: presetId });
}

export function setBrightness(ip: string, bri: number): Promise<NodeState> {
  return setState(ip, { bri: Math.max(0, Math.min(255, bri)) });
}

export function setPower(ip: string, on: boolean): Promise<NodeState> {
  return setState(ip, { on });
}

export function emergencyOff(ip: string): Promise<NodeState> {
  return setState(ip, { Lumigrid_PCA9685: { emergencyOff: true } } as Record<string, unknown>);
}

/** Set an individual pixel's RGBW color in segment 0. */
export function setPixelColor(ip: string, pixelIdx: number, r: number, g: number, b: number, w = 0): Promise<NodeState> {
  return setState(ip, { seg: [{ id: 0, i: [pixelIdx, [r, g, b, w]] }] });
}

/** Set multiple pixels at once in segment 0. */
export function setPixels(ip: string, pixels: Array<[number, [number, number, number, number]]>): Promise<NodeState> {
  const i: unknown[] = [];
  for (const [idx, [r, g, b, w]] of pixels) i.push(idx, [r, g, b, w]);
  return setState(ip, { seg: [{ id: 0, i }] });
}

// ── Filesystem ─────────────────────────────────────────────────────────────────

/** Upload a text file to LED Node Storage via /edit endpoint. */
export async function uploadFile(ip: string, filename: string, content: string): Promise<void> {
  const form = new FormData();
  form.append('data', new Blob([content], { type: 'application/json' }), filename);
  const res = await rawFetch(ip, '/edit', { method: 'POST', body: form });
  if (!res.ok) throw new Error(`Upload failed: HTTP ${res.status}`);
}

/** Save animations schedule to /animations.json on LED Node Storage. */
export async function saveSchedule(ip: string, blocks: AnimationBlock[]): Promise<void> {
  const json = JSON.stringify({ animations: blocks }, null, 2);
  return uploadFile(ip, 'animations.json', json);
}

// ── OTA ───────────────────────────────────────────────────────────────────────

/**
 * Upload firmware binary to a LumiGrid LED Node's /update endpoint.
 * Returns a promise that resolves when the upload is accepted (not when rebooted).
 */
export function uploadFirmware(
  ip: string,
  binary: File | Blob,
  onProgress?: (pct: number) => void
): Promise<void> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `http://${ip}/update`, true);
    xhr.setRequestHeader('Authorization', `Bearer ${getApiToken()}`);
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) onProgress(Math.round((e.loaded / e.total) * 100));
    };
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) resolve();
      else reject(new Error(`OTA failed: HTTP ${xhr.status}`));
    };
    xhr.onerror = () => reject(new Error('OTA network error'));
    xhr.ontimeout = () => reject(new Error('OTA timeout'));
    xhr.timeout = 120_000; // 2 min
    const form = new FormData();
    form.append('update', binary instanceof File ? binary : new File([binary], 'firmware.bin'), 'firmware.bin');
    xhr.send(form);
  });
}

// ── Status snapshot ───────────────────────────────────────────────────────────

/** Fetch info + state for a node and measure latency. Returns NodeStatus. */
export async function getNodeStatus(ip: string): Promise<NodeStatus> {
  const t0 = performance.now();
  try {
    const [info, state] = await Promise.all([getInfo(ip), getState(ip)]);
    return { ip, info, state, error: null, latency: Math.round(performance.now() - t0) };
  } catch (e) {
    return { ip, info: null, state: null, error: String(e), latency: Math.round(performance.now() - t0) };
  }
}

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
  return wfetch<ScenePreset[]>(ip, '/api/v1/presets');
}

export function savePreset(ip: string, preset: Partial<ScenePreset> & { index: number }): Promise<{ saved: true }> {
  return wfetch<{ saved: true }>(ip, '/api/v1/presets', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(preset),
  });
}

export function activatePreset(ip: string, index: number, durationMs = 60000): Promise<{ activated: true }> {
  return wfetch<{ activated: true }>(ip, '/api/v1/activate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ preset: index, durationMs }),
  });
}

export function getEffects(ip: string): Promise<EffectMeta[]> {
  return wfetch<EffectMeta[]>(ip, '/api/v1/effects');
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
  return wfetch<Sequence>(ip, '/api/v1/sequence');
}

export function saveSequence(ip: string, seq: Sequence): Promise<{ saved: true }> {
  return wfetch<{ saved: true }>(ip, '/api/v1/sequence', {
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
  return wfetch<CalendarSchedule>(ip, '/api/v1/schedules');
}

export function saveCalendarSchedule(ip: string, schedule: CalendarSchedule): Promise<{ saved: true }> {
  return wfetch<{ saved: true }>(ip, '/api/v1/schedules', {
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
  return wfetch<HealthSnapshot>(ip, '/api/v1/health');
}

export function getLogs(ip: string): Promise<string> {
  return rawFetch(ip, '/api/v1/logs').then(async res => {
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.text();
  });
}

export function getAuditLog(ip: string): Promise<AuditEvent[]> {
  return wfetch<AuditEvent[]>(ip, '/api/v1/audit');
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
  return wfetch<FactoryStatus>(ip, '/api/v1/factory/status');
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
  return wfetch<{ accepted: boolean }>(ip, '/api/v1/factory/effect', {
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
  return wfetch<{ accepted: boolean }>(ip, '/api/v1/factory/lane', {
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
  return wfetch<{ accepted: boolean }>(ip, '/api/v1/factory/pwm', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
}

export function postFactoryAbort(ip: string): Promise<{ accepted: boolean; action: string }> {
  return wfetch<{ accepted: boolean; action: string }>(ip, '/api/v1/factory/abort', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });
}

export function postFactoryAutoLoop(ip: string, enabled: boolean): Promise<{ accepted: boolean }> {
  return wfetch<{ accepted: boolean }>(ip, '/api/v1/factory/auto-loop', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ enabled }),
  });
}
