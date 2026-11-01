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
  channels?: number;
  pixelLanes: number;
  pwmChannels: number;
  groups?: number;
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
  manualControl?: boolean;
  artnet: boolean;
  sacn: boolean;
  sync: boolean;
  mqtt: boolean;
  factory: boolean;
  ota: boolean;
  signedOta: boolean;
  backup: boolean;
  showManifest?: boolean;
  playbackManifest?: boolean;
  fleetDiscovery?: boolean;
  sceneMotion?: boolean;
  cueLists?: boolean;
  transitionEngineV2?: boolean;
}

export interface CapabilitiesProtocols {
  httpApi: number;
  websocket: number;
  sync: number;
  discovery?: number;
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

export interface PublicNodeInfo {
  id: string;
  name: string;
  family: string;
  model: string;
  hardwareRevision: string;
  firmwareVersion: string;
  mac?: string;
}

import type { RuntimeSnapshotV3 } from '../state/runtimeSnapshot';
import { normalizeRuntimeSnapshot } from '../state/compat/runtimeSnapshotV2';
import { nodeAuthStore } from '../auth/nodeAuth';

export async function getNodeInfo(ip: string): Promise<PublicNodeInfo> {
  const info = await apiJson<PublicNodeInfo>(ip, '/api/v1/info');
  if (info && info.id) {
    nodeAuthStore.bindNodeId(ip, info.id);
  }
  return info;
}

export async function getStatus(ip: string): Promise<RuntimeSnapshotV3> {
  const raw = await apiJson<unknown>(ip, '/api/v1/status');
  const snap = normalizeRuntimeSnapshot(raw);
  if (snap?.node?.id && snap.node.id !== 'unknown') {
    nodeAuthStore.bindNodeId(ip, snap.node.id);
  }
  return snap;
}

export async function getIdentity(ip: string): Promise<NodeIdentity> {
  const ident = await apiJson<NodeIdentity>(ip, '/api/v1/identity');
  if (ident && ident.id) {
    nodeAuthStore.bindNodeId(ip, ident.id);
  }
  return ident;
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
} from './transport';

export {
  ApiError,
  apiFetch,
  apiJson,
  authHeaders,
};


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

