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
  readString,
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
}

export interface DashboardNodeState {
  ip: string;
  nodeIp: string;
  connection: NodeConnection;
  authenticated: boolean;
  role: NodeRole;
  lastSeen: number;
  lastMessageAt: number;
  status: NodeLiveStatus | null;
  health: HealthSnapshot | null;
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
};

const initial: NodeLiveState = {
  ip: '',
  nodeIp: '',
  connection: 'idle',
  authenticated: false,
  role: 'unknown',
  lastSeen: 0,
  lastMessageAt: 0,
  status: { ...EMPTY_STATUS },
  health: null,
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
    if (patch.ip !== undefined) next.nodeIp = patch.ip;
    if (patch.nodeIp !== undefined) next.ip = patch.nodeIp;
    if (patch.lastSeen !== undefined) next.lastMessageAt = patch.lastSeen;
    if (patch.lastMessageAt !== undefined) next.lastSeen = patch.lastMessageAt;
    return next;
  });
}

function scheduleReconnect() {
  if (manualClose || reconnectTimer || !getSelectedNodeIp()) return;
  const delay = Math.min(10000, 500 * Math.pow(2, reconnectAttempt));
  reconnectAttempt = Math.min(reconnectAttempt + 1, 5);
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, delay);
}

export async function refreshHealth(nodeIp = getSelectedNodeIp()): Promise<HealthSnapshot | null> {
  const ip = nodeIp.trim();
  if (!ip) return null;
  try {
    const health = await getHealth(ip);
    publish({ health, error: '' });
    return health;
  } catch (err) {
    return null;
  }
}

export function connect(nodeIp = getSelectedNodeIp()): void {
  if (typeof WebSocket === 'undefined') return;
  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) return;

  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }

  const ip = nodeIp.trim();
  if (!ip) {
    publish({ ip: '', nodeIp: '', connection: 'idle', error: '' });
    return;
  }

  manualClose = false;
  publish({
    ip,
    nodeIp: ip,
    connection: 'connecting',
    role: getStoredRole(),
    authenticated: isStoredAuthed(),
    error: '',
  });

  const ws = new WebSocket(`ws://${ip}/ws`);
  socket = ws;
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    if (socket !== ws) return;
    reconnectAttempt = 0;
    publish({
      connection: 'connected',
      role: getStoredRole(),
      authenticated: isStoredAuthed(),
      error: '',
    });

    // Proactively pull health snapshot on connect
    refreshHealth(ip).catch(() => {});

    // Periodic health poll while connected (every 10 seconds)
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
          },
          lastSeen: now,
          lastMessageAt: now,
        }));
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
  connect(ip);
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
