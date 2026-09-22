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

// ── Control Plane v3 Authoritative Identity & Capabilities ──────────────────────

export interface NodeIdentity {
  id: string;
  name: string;
  family: string;
  model: string;
  hardwareRevision: string;
  firmwareVersion: string;
}

export interface CapabilitiesLimits {
  pixelLanes: number;
  pwmChannels: number;
  zones: number;
  layersPerZone: number;
  presets: number;
  timelineSteps: number;
  cueLists?: number;
  cuesPerList?: number;
  automationTracks?: number;
  automationPoints?: number;
}

export interface CapabilitiesFeatures {
  addressable: boolean;
  pwm: boolean;
  artnet: boolean;
  sacn: boolean;
  sync: boolean;
  mqtt: boolean;
  factory: boolean;
  ota: boolean;
  signedOta: boolean;
  backup: boolean;
  sceneMotion?: boolean;
  cueLists?: boolean;
  transitionEngineV2?: boolean;
}

export interface CapabilitiesProtocols {
  httpApi: number;
  websocket: number;
  sync: number;
}

export interface CapabilitiesManifest {
  schema: number;
  node: {
    family: string;
    model: string;
    firmware: string;
    hardwareRevision: string;
  };
  limits: CapabilitiesLimits;
  features: CapabilitiesFeatures;
  protocols: CapabilitiesProtocols;
}

export function getIdentity(ip: string): Promise<NodeIdentity> {
  return apiJson<NodeIdentity>(ip, '/api/v1/identity');
}

export function getCapabilities(ip: string): Promise<CapabilitiesManifest> {
  return apiJson<CapabilitiesManifest>(ip, '/api/v1/capabilities');
}

// ── Shared transport ─────────────────────────────────────────────────────────

import {
  ApiError,
  apiFetch,
  apiJson,
  authHeaders,
  clearApiToken,
  DEV_API_TOKEN,
  getApiToken,
  setApiToken,
} from './transport';

export {
  ApiError,
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
  getNodeStatus,
} from './legacy';

// ── Domain module re-exports: Scenes & Timeline ──────────────────────────────
export * from './scenes';
export * from './timeline';

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

// ── Domain module re-export: Diagnostics ───────────────────────────────────────
export * from './diagnostics';

// ── Domain module re-export: Factory ───────────────────────────────────────────
export * from './factory';

// ── Domain module re-export: Config & Recovery ─────────────────────────────────
export * from './config';

// ── Domain module re-export: Firmware ──────────────────────────────────────────
export * from './firmware';

// ── Domain module re-export: Scenes ────────────────────────────────────────────
export * from './scenes';

