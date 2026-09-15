/**
 * Shared browser-side node state.
 *
 * The node remains the source of truth. This store only owns connection state,
 * the latest WebSocket snapshot and the selected node address so dashboard
 * routes do not each implement their own polling/socket lifecycle.
 */

import { NODE_IP_KEY, readString, writeString } from '../auth/storage';

export type NodeConnectionState = 'idle' | 'connecting' | 'connected' | 'disconnected' | 'error';

export interface NodeLiveStatus {
  effect: string;
  effectStartedMs: number;
  durationMs: number;
  brightness: number;
  autoLoop: boolean;
  outputsEnabled: boolean;
  testActive: boolean;
  commandRejections: number;
}

export interface NodeStoreSnapshot {
  nodeIp: string;
  connection: NodeConnectionState;
  status: NodeLiveStatus;
  pixels: Uint8Array | null;
  pixelFrames: number;
  lastMessageAt: number;
  reconnectAttempt: number;
  error: string;
}

const EMPTY_STATUS: NodeLiveStatus = {
  effect: '',
  effectStartedMs: 0,
  durationMs: 0,
  brightness: 0,
  autoLoop: false,
  outputsEnabled: false,
  testActive: false,
  commandRejections: 0,
};

const MAX_RECONNECT_DELAY_MS = 10_000;
const PIXEL_FRAME_MAX_BYTES = 8 * 144 * 3 + 1;

type Listener = (snapshot: NodeStoreSnapshot) => void;

class NodeStore {
  private socket: WebSocket | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private listeners = new Set<Listener>();
  private snapshot: NodeStoreSnapshot = {
    nodeIp: '',
    connection: 'idle',
    status: { ...EMPTY_STATUS },
    pixels: null,
    pixelFrames: 0,
    lastMessageAt: 0,
    reconnectAttempt: 0,
    error: '',
  };

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    listener(this.snapshot);
    return () => this.listeners.delete(listener);
  }

  getSnapshot(): NodeStoreSnapshot {
    return this.snapshot;
  }

  setNodeIp(nodeIp: string): void {
    const normalized = nodeIp.trim().replace(/^https?:\/\//, '').replace(/\/$/, '');
    if (normalized === this.snapshot.nodeIp) return;

    this.disconnect(false);
    this.snapshot = {
      ...this.snapshot,
      nodeIp: normalized,
      connection: normalized ? 'idle' : 'disconnected',
      reconnectAttempt: 0,
      error: '',
    };
    if (normalized) writeString(NODE_IP_KEY, normalized);
    this.emit();
  }

  loadNode(): string {
    const nodeIp = readString(NODE_IP_KEY, '').trim();
    this.setNodeIp(nodeIp);
    return nodeIp;
  }

  connect(): void {
    if (typeof WebSocket === 'undefined' || !this.snapshot.nodeIp) return;
    if (this.socket && (this.socket.readyState === WebSocket.OPEN || this.socket.readyState === WebSocket.CONNECTING)) return;

    this.clearReconnectTimer();
    this.snapshot = { ...this.snapshot, connection: 'connecting', error: '' };
    this.emit();

    const scheme = typeof location !== 'undefined' && location.protocol === 'https:' ? 'wss' : 'ws';
    const socket = new WebSocket(`${scheme}://${this.snapshot.nodeIp}/ws`);
    socket.binaryType = 'arraybuffer';
    this.socket = socket;

    socket.onopen = () => {
      if (this.socket !== socket) return;
      this.snapshot = { ...this.snapshot, connection: 'connected', reconnectAttempt: 0, error: '' };
      this.emit();
    };

    socket.onmessage = (event) => {
      if (this.socket !== socket) return;
      this.handleMessage(event.data);
    };

    socket.onerror = () => {
      if (this.socket !== socket) return;
      this.snapshot = { ...this.snapshot, connection: 'error', error: 'WebSocket connection failed' };
      this.emit();
    };

    socket.onclose = () => {
      if (this.socket !== socket) return;
      this.socket = null;
      this.snapshot = { ...this.snapshot, connection: 'disconnected' };
      this.emit();
      this.scheduleReconnect();
    };
  }

  disconnect(clearNode = false): void {
    this.clearReconnectTimer();
    const socket = this.socket;
    this.socket = null;
    if (socket) {
      socket.onclose = null;
      socket.close();
    }
    this.snapshot = {
      ...this.snapshot,
      nodeIp: clearNode ? '' : this.snapshot.nodeIp,
      connection: clearNode || !this.snapshot.nodeIp ? 'disconnected' : 'idle',
      reconnectAttempt: 0,
    };
    this.emit();
  }

  destroy(): void {
    this.disconnect();
    this.listeners.clear();
  }

  private handleMessage(data: unknown): void {
    const now = Date.now();

    if (data instanceof ArrayBuffer) {
      const frame = new Uint8Array(data);
      if (frame.length > 1 && (frame[0] === 1 || frame[0] === 76) && frame.length <= PIXEL_FRAME_MAX_BYTES) {
        this.snapshot = {
          ...this.snapshot,
          pixels: new Uint8Array(frame.subarray(1)),
          pixelFrames: this.snapshot.pixelFrames + 1,
          lastMessageAt: now,
        };
        this.emit();
      }
      return;
    }

    if (typeof data !== 'string') return;

    try {
      const value: unknown = JSON.parse(data);
      if (!value || typeof value !== 'object') return;
      const json = value as Record<string, unknown>;
      this.snapshot = {
        ...this.snapshot,
        status: {
          effect: typeof json.effect === 'string' ? json.effect : this.snapshot.status.effect,
          effectStartedMs: numberValue(json.effectStartedMs, this.snapshot.status.effectStartedMs),
          durationMs: numberValue(json.durationMs, this.snapshot.status.durationMs),
          brightness: numberValue(json.brightness, this.snapshot.status.brightness),
          autoLoop: boolValue(json.autoLoop, this.snapshot.status.autoLoop),
          outputsEnabled: boolValue(json.outputsEnabled, this.snapshot.status.outputsEnabled),
          testActive: boolValue(json.testActive, this.snapshot.status.testActive),
          commandRejections: numberValue(json.commandRejections, this.snapshot.status.commandRejections),
        },
        lastMessageAt: now,
      };
      this.emit();
    } catch {
      // A malformed frame must not tear down the shared node connection.
    }
  }

  private scheduleReconnect(): void {
    if (!this.snapshot.nodeIp || this.reconnectTimer) return;
    const attempt = this.snapshot.reconnectAttempt + 1;
    const delay = Math.min(1000 * 2 ** Math.min(attempt - 1, 4), MAX_RECONNECT_DELAY_MS);
    this.snapshot = { ...this.snapshot, reconnectAttempt: attempt };
    this.emit();
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, delay);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
  }

  private emit(): void {
    for (const listener of this.listeners) listener(this.snapshot);
  }
}

function numberValue(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function boolValue(value: unknown, fallback: boolean): boolean {
  return typeof value === 'boolean' ? value : fallback;
}

export const nodeStore = new NodeStore();
