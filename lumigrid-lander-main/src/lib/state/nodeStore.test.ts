import { describe, expect, it, beforeEach, afterEach, vi } from 'vitest';
import {
  connect,
  disconnect,
  getNodeLiveSnapshot,
  getNodeLiveState,
  nodePixelStore,
  nodeFrameCount,
  BUF_SIZE,
} from './nodeStore';

class MockWebSocket {
  static instances: MockWebSocket[] = [];
  readyState = 0;
  binaryType = 'blob';
  url: string;
  onopen: (() => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  onmessage: ((event: { data: unknown }) => void) | null = null;

  constructor(url: string) {
    this.url = url;
    MockWebSocket.instances.push(this);
    setTimeout(() => {
      this.readyState = 1;
      this.onopen?.();
    }, 0);
  }

  close() {
    this.readyState = 3;
    this.onclose?.();
  }
}

describe('unified dashboard node live state', () => {
  beforeEach(() => {
    MockWebSocket.instances = [];
    vi.stubGlobal('WebSocket', MockWebSocket);
    disconnect();
  });

  afterEach(() => {
    disconnect();
    vi.unstubAllGlobals();
  });

  it('starts with idle or disconnected connection and empty state', () => {
    const snap = getNodeLiveSnapshot();
    expect(['idle', 'disconnected']).toContain(snap.connection);
    expect(snap.status).toBeDefined();
    expect(snap.pixels.length).toBe(BUF_SIZE);
  });

  it('connects to node WebSocket and updates state on JSON message', async () => {
    connect('192.168.1.99');
    const ws = MockWebSocket.instances[0];
    expect(ws).toBeDefined();
    expect(ws.url).toBe('ws://192.168.1.99/ws');

    // Wait for open
    await new Promise(r => setTimeout(r, 10));
    expect(getNodeLiveSnapshot().connection).toBe('connected');

    // Send JSON status frame
    ws.onmessage?.({
      data: JSON.stringify({
        effect: 'rainbow',
        brightness: 180,
        outputsEnabled: true,
        testActive: false,
        autoLoop: true,
      }),
    });

    const snap = getNodeLiveSnapshot();
    expect(snap.status?.effect).toBe('rainbow');
    expect(snap.status?.brightness).toBe(180);
    expect(snap.status?.outputsEnabled).toBe(true);
    expect(snap.lastSeen).toBeGreaterThan(0);
  });

  it('handles binary pixel frames defensively without throwing', async () => {
    connect('192.168.1.99');
    const ws = MockWebSocket.instances[0];
    await new Promise(r => setTimeout(r, 10));

    // Valid header byte (1), followed by 3 bytes RGB
    const frame = new Uint8Array([1, 255, 0, 128]);
    ws.onmessage?.({ data: frame.buffer });

    const snap = getNodeLiveSnapshot();
    expect(snap.frameCount).toBe(1);
    expect(snap.pixels[0]).toBe(255);
    expect(snap.pixels[1]).toBe(0);
    expect(snap.pixels[2]).toBe(128);
  });

  it('ignores malformed messages without corrupting state', async () => {
    connect('192.168.1.99');
    const ws = MockWebSocket.instances[0];
    await new Promise(r => setTimeout(r, 10));

    // Send broken JSON
    ws.onmessage?.({ data: '{ not valid json ' });
    const snap = getNodeLiveSnapshot();
    expect(snap.connection).toBe('connected');
    expect(snap.status).toBeDefined();
  });
});
