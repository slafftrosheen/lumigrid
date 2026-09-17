/**
 * Shared live node connection/state for dashboard views.
 *
 * The node remains the source of truth. This module owns client-side
 * connection state, high-frequency pixel buffers, live status, and health
 * metadata without pages opening competing WebSockets or duplicating state.
 */

import { writable } from 'svelte/store';
import {
  AUTH_ROLE_KEY,
  AUTH_TIMESTAMP_KEY,
  getSelectedNodeIp,
  setSelectedNodeIp,
  readString,
  updateNodeLastSeen,
} from '../auth/storage';
import { getHealth, type HealthSnapshot } from '../api/node';

export const LANES = 8;
export const PIXELS_PER_LANE = 144;
export const BUF_SIZE = LANES * PIXELS_PER_LANE * 3;

export type NodeConnection = 'idle' | 'connecting' | 'connected' | 'disconnected' | 'error';
export type NodeRole = 'admin' | 'installer' | 'operator' | 'unknown';

export interface NodeLiveStatus {
  effect: string;
  brightness: number;
  outputsEnabled: boolean;
  testActive: boolean;
  autoLoop: boolean;
  effectStartedMs?: number;
  durationMs?: number;
  commandRejections?: number;
  activePreset?: number;
}

export interface DashboardNodeState {
  nodeIp: string;
  ip: string;
  connection: NodeConnection;
  /** True if local dashboard PIN wall is unlocked. Firmware remains authoritative. */
  dashboardUnlocked: boolean;
  dashboardRole: 'admin' | 'operator' | 'installer' | 'unknown';
  /** @deprecated Use dashboardUnlocked. Indicates local PIN wall state, NOT firmware auth. */
  authenticated: boolean;
  /** @deprecated Use dashboardRole. Indicates local UI role, NOT firmware auth. */
  role: NodeRole;
  lastSeen: number;
  lastMessageAt: number;
  status: NodeLiveStatus | null;
  health: HealthSnapshot | null;
  healthError: string;
  healthUpdatedAt: number;
  error: string;
}

export interface NodeLiveState extends DashboardNodeState {
  pixels: Uint8Array;
  frameCount: number;
}

const EMPTY_STATUS: NodeLiveStatus = {
  effect: '',
  brightness: 0,
  outputsEnabled: false,
  testActive: false,
  autoLoop: false,
  effectStartedMs: 0,
  durationMs: 0,
  commandRejections: 0,
  activePreset: -1,
};

const initial: NodeLiveState = {
  nodeIp: '',
  ip: '',
  connection: 'idle',
  dashboardUnlocked: false,
  dashboardRole: 'unknown',
  authenticated: false,
  role: 'unknown',
  lastSeen: 0,
  lastMessageAt: 0,
  status: { ...EMPTY_STATUS },
  health: null,
  healthError: '',
  healthUpdatedAt: 0,
  pixels: new Uint8Array(BUF_SIZE),
  frameCount: 0,
  error: '',
};

// Main node state store (low-frequency metadata)
const state = writable<NodeLiveState>(initial);
let latestState = initial;
state.subscribe(value => { latestState = value; });

// Separate pixel buffer store for high-frequency renders without waking up unrelated UI components
export const nodePixelStore = writable<Uint8Array>(new Uint8Array(BUF_SIZE));
export const nodeFrameCount = writable<number>(0);

let socket: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let healthPollTimer: ReturnType<typeof setInterval> | null = null;
let reconnectAttempt = 0;
let manualClose = false;

function getStoredRole(): NodeRole {
  const r = readString(AUTH_ROLE_KEY);
  if (r === 'admin') return 'admin';
  if (r === 'installer') return 'installer';
  if (r === 'operator') return 'operator';
  return 'unknown';
}

function isStoredAuthed(): boolean {
  const ts = Number(readString(AUTH_TIMESTAMP_KEY, '0'));
  return Date.now() - ts < 8 * 3600 * 1000;
}

function publish(patch: Partial<NodeLiveState>) {
  state.update(current => {
    const next = { ...current, ...patch };
    const ipVal = patch.nodeIp ?? patch.ip;
    if (ipVal !== undefined) {
      next.nodeIp = ipVal;
      next.ip = ipVal;
    }
    const msgVal = patch.lastMessageAt ?? patch.lastSeen;
    if (msgVal !== undefined) {
      next.lastMessageAt = msgVal;
      next.lastSeen = msgVal;
    }
    return next;
  });
}

export function getWsUrl(ip: string): string {
  const scheme =
    typeof location !== 'undefined' && location.protocol === 'https:'
      ? 'wss'
      : 'ws';
  return `${scheme}://${ip}/ws`;
}

let lastFleetUpdate = 0;
function recordFleetPresence(ip: string) {
  if (!ip) return;
  const now = Date.now();
  if (now - lastFleetUpdate > 5000) {
    lastFleetUpdate = now;
    try { updateNodeLastSeen(ip); } catch {}
  }
}

function scheduleReconnect() {
  if (manualClose || reconnectTimer) return;
  const targetIp = latestState.nodeIp || getSelectedNodeIp();
  if (!targetIp) return;

  const delay = Math.min(10000, 500 * Math.pow(2, reconnectAttempt));
  reconnectAttempt = Math.min(reconnectAttempt + 1, 5);
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect(targetIp);
  }, delay);
}

export async function refreshHealth(nodeIp = latestState.nodeIp || getSelectedNodeIp()): Promise<HealthSnapshot | null> {
  const ip = nodeIp.trim();
  if (!ip) return null;
  try {
    const health = await getHealth(ip);
    publish({ health, healthError: '', healthUpdatedAt: Date.now() });
    recordFleetPresence(ip);
    return health;
  } catch (err) {
    const healthError = err instanceof Error ? err.message : String(err);
    publish({ healthError, healthUpdatedAt: Date.now() });
    return null;
  }
}

export function connect(nodeIp = getSelectedNodeIp()): void {
  if (typeof WebSocket === 'undefined') return;

  const ip = nodeIp.trim();
  if (!ip) {
    setSelectedNodeIp('');
    if (socket) {
      const ws = socket;
      socket = null;
      ws.close();
    }
    publish({ nodeIp: '', connection: 'idle', error: '' });
    return;
  }
  setSelectedNodeIp(ip);

  // If already connected/connecting to the exact same IP, return early
  if (socket && latestState.nodeIp === ip && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
    return;
  }

  // If switching to a different node, close existing socket and clear reconnect timer
  if (socket && latestState.nodeIp !== ip) {
    const ws = socket;
    socket = null;
    ws.close();
  }

  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  if (healthPollTimer) {
    clearInterval(healthPollTimer);
    healthPollTimer = null;
  }

  manualClose = false;
  const currentRole = getStoredRole();
  const currentUnlocked = isStoredAuthed();
  publish({
    nodeIp: ip,
    connection: 'connecting',
    dashboardRole: currentRole,
    dashboardUnlocked: currentUnlocked,
    role: currentRole,
    authenticated: currentUnlocked,
    error: '',
  });

  const ws = new WebSocket(getWsUrl(ip));
  socket = ws;
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    if (socket !== ws) return;
    reconnectAttempt = 0;
    const openRole = getStoredRole();
    const openUnlocked = isStoredAuthed();
    publish({
      connection: 'connected',
      dashboardRole: openRole,
      dashboardUnlocked: openUnlocked,
      role: openRole,
      authenticated: openUnlocked,
      error: '',
    });

    // Proactively pull health snapshot on connect
    refreshHealth(ip).catch(() => {});

    // Periodic health poll while connected (every 10 seconds, single interval guaranteed)
    if (healthPollTimer) clearInterval(healthPollTimer);
    healthPollTimer = setInterval(() => {
      if (socket && socket.readyState === WebSocket.OPEN) {
        refreshHealth(ip).catch(() => {});
      }
    }, 10000);
  };

  ws.onclose = () => {
    if (socket !== ws) return;
    socket = null;
    if (healthPollTimer) {
      clearInterval(healthPollTimer);
      healthPollTimer = null;
    }
    publish({ connection: 'disconnected' });
    scheduleReconnect();
  };

  ws.onerror = () => {
    if (socket !== ws) return;
    publish({ connection: 'error', error: 'WebSocket connection error' });
  };

  ws.onmessage = event => {
    if (socket !== ws) return;
    const now = Date.now();

    if (event.data instanceof ArrayBuffer) {
      const arr = new Uint8Array(event.data);
      if (arr.length > 1 && (arr[0] === 1 || arr[0] === 76)) {
        const raw = arr.subarray(1);
        const len = Math.min(raw.length, BUF_SIZE);
        const pixels = new Uint8Array(BUF_SIZE);
        pixels.set(raw.subarray(0, len));

        // Update dedicated pixel stores
        nodePixelStore.set(pixels);
        nodeFrameCount.update(n => n + 1);

        // Update main state snapshot
        state.update(current => ({
          ...current,
          pixels,
          frameCount: current.frameCount + 1,
          lastSeen: now,
          lastMessageAt: now,
        }));
      }
      return;
    }

    if (typeof event.data === 'string') {
      try {
        const json = JSON.parse(event.data) as Record<string, unknown>;
        const hasStatusFields =
          json.type === 'status' ||
          'effect' in json ||
          'brightness' in json ||
          'outputsEnabled' in json ||
          'testActive' in json ||
          'autoLoop' in json ||
          'activePreset' in json;
        if (!hasStatusFields) return;

        state.update(current => ({
          ...current,
          status: {
            effect: typeof json.effect === 'string' ? json.effect : (current.status?.effect ?? ''),
            brightness: typeof json.brightness === 'number' ? json.brightness : (current.status?.brightness ?? 0),
            outputsEnabled: typeof json.outputsEnabled === 'boolean' ? json.outputsEnabled : (current.status?.outputsEnabled ?? false),
            testActive: typeof json.testActive === 'boolean' ? json.testActive : (current.status?.testActive ?? false),
            autoLoop: typeof json.autoLoop === 'boolean' ? json.autoLoop : (current.status?.autoLoop ?? false),
            effectStartedMs: typeof json.effectStartedMs === 'number' ? json.effectStartedMs : current.status?.effectStartedMs,
            durationMs: typeof json.durationMs === 'number' ? json.durationMs : current.status?.durationMs,
            commandRejections: typeof json.commandRejections === 'number' ? json.commandRejections : current.status?.commandRejections,
            activePreset: typeof json.activePreset === 'number' ? json.activePreset : current.status?.activePreset,
          },
          lastSeen: now,
          lastMessageAt: now,
        }));
        recordFleetPresence(latestState.nodeIp);
      } catch {
        // Defensive: ignore malformed status frames; maintain last known good state.
      }
    }
  };
}

export function disconnect(): void {
  manualClose = true;
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  if (healthPollTimer) {
    clearInterval(healthPollTimer);
    healthPollTimer = null;
  }
  reconnectAttempt = 0;
  const ws = socket;
  socket = null;
  if (ws) ws.close();
  publish({ connection: 'disconnected' });
}

export function reconnect(ip?: string): void {
  disconnect();
  manualClose = false;
  const target = ip ?? latestState.nodeIp ?? getSelectedNodeIp();
  connect(target);
}

export const nodeStore = state;

export function selectNode(ip: string): void {
  const target = (ip || '').trim();
  setSelectedNodeIp(target);
  if (!target) {
    disconnect();
    publish({ nodeIp: '', ip: '', connection: 'idle' });
  } else {
    connect(target);
  }
}

export function getNodeLiveState() {
  return state;
}

export function getNodeLiveSnapshot(): NodeLiveState {
  return {
    ...latestState,
    status: latestState.status ? { ...latestState.status } : null,
    health: latestState.health ? { ...latestState.health } : null,
    pixels: latestState.pixels.slice(),
  };
}

export function _resetForTesting(): void {
  disconnect();
  manualClose = false;
  reconnectAttempt = 0;
  state.set({ ...initial, status: { ...EMPTY_STATUS }, pixels: new Uint8Array(BUF_SIZE) });
  nodePixelStore.set(new Uint8Array(BUF_SIZE));
  nodeFrameCount.set(0);
}
