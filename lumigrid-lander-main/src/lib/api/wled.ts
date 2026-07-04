/**
 * WLED HTTP API client for LumiGrid Operator Dashboard.
 * All functions return typed responses; errors throw with a human-readable message.
 */

// ── Types ─────────────────────────────────────────────────────────────────────

export interface WledInfo {
  ver:    string;
  name:   string;
  uptime: number;
  freeheap: number;
  ip:     string;
  mac:    string;
  u?: Record<string, Record<string, unknown>>;
}

export interface WledSegment {
  id:  number;
  start: number;
  stop: number;
  len: number;
  on:  boolean;
  bri: number;
  col: number[][];
  fx:  number;
  pal: number;
  i?:  unknown[];
}

export interface WledState {
  on:  boolean;
  bri: number;
  transition: number;
  ps:  number;
  seg: WledSegment[];
}

export interface WledNode {
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
  info:    WledInfo | null;
  state:   WledState | null;
  error:   string | null;
  latency: number;
}

// ── Core fetch helper ─────────────────────────────────────────────────────────

async function wfetch<T>(ip: string, path: string, opts?: RequestInit, timeout = 8000): Promise<T> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeout);
  const url = `http://${ip}${path}`;
  try {
    const res = await fetch(url, { ...opts, signal: ctrl.signal });
    clearTimeout(timer);
    if (!res.ok) throw new Error(`HTTP ${res.status} ${res.statusText}`);
    return (await res.json()) as T;
  } catch (e: unknown) {
    clearTimeout(timer);
    const msg = e instanceof Error ? e.message : String(e);
    throw new Error(`[${ip}] ${path} → ${msg}`);
  }
}

// ── Read endpoints ─────────────────────────────────────────────────────────────

export function getInfo(ip: string): Promise<WledInfo> {
  return wfetch<WledInfo>(ip, '/json/info');
}

export function getState(ip: string): Promise<WledState> {
  return wfetch<WledState>(ip, '/json/state');
}

export function getNodes(ip: string): Promise<{ nodes: WledNode[] }> {
  return wfetch<{ nodes: WledNode[] }>(ip, '/json/nodes');
}

export async function getChannelConfig(ip: string): Promise<ChannelConfig | null> {
  try {
    const cfg = await wfetch<{ um?: { Lumigrid_PCA9685?: ChannelConfig } }>(ip, '/json/cfg');
    return cfg?.um?.Lumigrid_PCA9685 ?? null;
  } catch {
    return null;
  }
}

/** Read /animations.json from WLED filesystem. Returns null if not found. */
export async function getSchedule(ip: string): Promise<AnimationBlock[] | null> {
  try {
    const ctrl = new AbortController();
    setTimeout(() => ctrl.abort(), 8000);
    const res = await fetch(`http://${ip}/animations.json`, { signal: ctrl.signal });
    if (!res.ok) return null;
    const j = await res.json() as { animations?: AnimationBlock[] };
    return j.animations ?? [];
  } catch {
    return null;
  }
}

// ── Write endpoints ────────────────────────────────────────────────────────────

export function setState(ip: string, state: Partial<WledState> | Record<string, unknown>): Promise<WledState> {
  return wfetch<WledState>(ip, '/json/state', {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(state)
  });
}

export function applyPreset(ip: string, presetId: number): Promise<WledState> {
  return setState(ip, { ps: presetId });
}

export function setBrightness(ip: string, bri: number): Promise<WledState> {
  return setState(ip, { bri: Math.max(0, Math.min(255, bri)) });
}

export function setPower(ip: string, on: boolean): Promise<WledState> {
  return setState(ip, { on });
}

export function emergencyOff(ip: string): Promise<WledState> {
  return setState(ip, { Lumigrid_PCA9685: { emergencyOff: true } } as Record<string, unknown>);
}

/** Set an individual pixel's RGBW color in segment 0. */
export function setPixelColor(ip: string, pixelIdx: number, r: number, g: number, b: number, w = 0): Promise<WledState> {
  return setState(ip, { seg: [{ id: 0, i: [pixelIdx, [r, g, b, w]] }] });
}

/** Set multiple pixels at once in segment 0. */
export function setPixels(ip: string, pixels: Array<[number, [number, number, number, number]]>): Promise<WledState> {
  const i: unknown[] = [];
  for (const [idx, [r, g, b, w]] of pixels) i.push(idx, [r, g, b, w]);
  return setState(ip, { seg: [{ id: 0, i }] });
}

// ── Filesystem ─────────────────────────────────────────────────────────────────

/** Upload a text file to WLED filesystem via /edit endpoint. */
export async function uploadFile(ip: string, filename: string, content: string): Promise<void> {
  const form = new FormData();
  form.append('data', new Blob([content], { type: 'application/json' }), filename);
  const res = await fetch(`http://${ip}/edit`, { method: 'POST', body: form });
  if (!res.ok) throw new Error(`Upload failed: HTTP ${res.status}`);
}

/** Save animations schedule to /animations.json on WLED filesystem. */
export async function saveSchedule(ip: string, blocks: AnimationBlock[]): Promise<void> {
  const json = JSON.stringify({ animations: blocks }, null, 2);
  return uploadFile(ip, 'animations.json', json);
}

// ── OTA ───────────────────────────────────────────────────────────────────────

/**
 * Upload firmware binary to a WLED node's /update endpoint.
 * Returns a promise that resolves when the upload is accepted (not when rebooted).
 */
export async function uploadFirmware(
  ip: string,
  binary: File | Blob,
  onProgress?: (pct: number) => void
): Promise<void> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', `http://${ip}/update`, true);
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

// ── Schedule types (shared) ────────────────────────────────────────────────────

export interface AnimationBlock {
  id:                string;
  file:              string;
  start_time:        string; // "HH:MM"
  duration_minutes:  number;
  fallback_preset:   number;
}
