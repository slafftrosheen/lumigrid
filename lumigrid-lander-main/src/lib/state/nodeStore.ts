/**
 * Shared live node connection/state for dashboard views.
 *
 * The node remains the source of truth. This module only owns browser-side
 * connection state and the latest live frame so pages do not open competing
 * WebSocket connections or read localStorage directly.
 */

import { writable } from 'svelte/store';
import { NODE_IP_KEY, readString } from '../auth/storage';

export const LANES = 8;
export const PIXELS_PER_LANE = 144;
export const BUF_SIZE = LANES * PIXELS_PER_LANE * 3;

export type NodeConnection = 'idle' | 'connecting' | 'connected' | 'disconnected' | 'error';

export interface NodeLiveStatus {
  effect: string;
  brightness: number;
  outputsEnabled: boolean;
  testActive: boolean;
  autoLoop: boolean;
}

export interface NodeLiveState {
  nodeIp: string;
  connection: NodeConnection;
  status: NodeLiveStatus;
  pixels: Uint8Array;
  frameCount: number;
  lastMessageAt: number;
  error: string;
}

const initial: NodeLiveState = {
  nodeIp: '',
  connection: 'idle',
  status: { effect: '', brightness: 0, outputsEnabled: false, testActive: false, autoLoop: false },
  pixels: new Uint8Array(BUF_SIZE),
  frameCount: 0,
  lastMessageAt: 0,
  error: '',
};

const state = writable<NodeLiveState>(initial);
let latestState = initial;
state.subscribe(value => { latestState = value; });

let socket: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let reconnectAttempt = 0;
let manualClose = false;

function publish(patch: Partial<NodeLiveState>) {
  state.update(current => ({ ...current, ...patch }));
}

function scheduleReconnect() {
  if (manualClose || reconnectTimer || !readString(NODE_IP_KEY)) return;
  const delay = Math.min(10000, 500 * Math.pow(2, reconnectAttempt));
  reconnectAttempt = Math.min(reconnectAttempt + 1, 5);
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, delay);
}

export function connect(nodeIp = readString(NODE_IP_KEY)): void {
  if (typeof WebSocket === 'undefined') return;
  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) return;

  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }

  const ip = nodeIp.trim();
  if (!ip) {
    publish({ nodeIp: '', connection: 'idle', error: '' });
    return;
  }

  manualClose = false;
  publish({ nodeIp: ip, connection: 'connecting', error: '' });

  const ws = new WebSocket(`ws://${ip}/ws`);
  socket = ws;
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    if (socket !== ws) return;
    reconnectAttempt = 0;
    publish({ connection: 'connected', error: '' });
  };

  ws.onclose = () => {
    if (socket !== ws) return;
    socket = null;
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
        const pixels = latestState.pixels;
        const raw = arr.subarray(1);
        const len = Math.min(raw.length, BUF_SIZE);
        pixels.fill(0);
        pixels.set(raw.subarray(0, len));
        state.update(current => ({ ...current, pixels, frameCount: current.frameCount + 1, lastMessageAt: now }));
      }
      return;
    }

    if (typeof event.data === 'string') {
      try {
        const json = JSON.parse(event.data) as Record<string, unknown>;
        state.update(current => ({
          ...current,
          status: {
            effect: typeof json.effect === 'string' ? json.effect : '',
            brightness: typeof json.brightness === 'number' ? json.brightness : 0,
            outputsEnabled: json.outputsEnabled === true,
            testActive: json.testActive === true,
            autoLoop: json.autoLoop === true,
          },
          lastMessageAt: now,
        }));
      } catch {
        // Ignore malformed status frames; keep the last known good state.
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
  reconnectAttempt = 0;
  const ws = socket;
  socket = null;
  if (ws) ws.close();
  publish({ connection: 'disconnected' });
}

export function reconnect(): void {
  disconnect();
  manualClose = false;
  connect();
}

export function getNodeLiveState() {
  return state;
}

export function getNodeLiveSnapshot(): NodeLiveState {
  return latestState;
}
