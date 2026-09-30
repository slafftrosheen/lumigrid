import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import {
  getSelectedNodeIp,
  setSelectedNodeIp,
  getSavedNodes,
  addSavedNode,
} from '../auth/storage';
import {
  apiJson,
  getPresets,
  savePreset,
  getSequence,
  saveSequence,
  getFactoryStatus,
  postFactoryAbort,
} from './node';
import { nodeAuthStore } from '../auth/nodeAuth';
import {
  connect,
  disconnect,
  reconnect,
  getNodeLiveSnapshot,
} from '../state/nodeStore';
import { createDefaultPreset } from './scenes';
import { createDefaultSequence } from './timeline';
import * as authApi from './auth';

class MockStorage implements Storage {
  private store: Record<string, string> = {};
  get length() { return Object.keys(this.store).length; }
  clear() { this.store = {}; }
  getItem(key: string): string | null { return this.store[key] ?? null; }
  key(index: number): string | null { return Object.keys(this.store)[index] ?? null; }
  removeItem(key: string): void { delete this.store[key]; }
  setItem(key: string, value: string): void { this.store[key] = String(value); }
}

class MockWebSocket {
  static instances: MockWebSocket[] = [];
  readyState = 0;
  binaryType = 'blob';
  url: string;
  onopen: (() => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  onmessage: ((event: { data: unknown }) => void) | null = null;

  protocols?: string | string[];

  constructor(url: string, protocols?: string | string[]) {
    this.url = url;
    this.protocols = protocols;
    MockWebSocket.instances.push(this);
    setTimeout(() => {
      this.readyState = 1;
      this.onopen?.();
    }, 5);
  }

  close() {
    this.readyState = 3;
    this.onclose?.();
  }
}

describe('dashboard contract integration tests', () => {
  let mockStorage: MockStorage;

  beforeEach(() => {
    mockStorage = new MockStorage();
    MockWebSocket.instances = [];
    vi.stubGlobal('localStorage', mockStorage);
    vi.stubGlobal('WebSocket', MockWebSocket);
    nodeAuthStore.clearAllSessions();
    nodeAuthStore.setSession({
      address: '192.168.1.100',
      nodeId: 'node-100',
      role: 'admin',
      sessionToken: 'test-session-token-100',
      expiresAt: Date.now() + 3600000,
    });
    vi.spyOn(authApi, 'createWsTicket').mockImplementation(async (ip: string) => ({
      ticket: `ws-ticket-${ip.replace(/\./g, '-')}`,
      expiresInMs: 15000,
    }));
    disconnect();
  });

  afterEach(() => {
    disconnect();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it('dashboard loads cleanly without a selected node', () => {
    const ip = getSelectedNodeIp();
    expect(ip).toBe('');

    const snap = getNodeLiveSnapshot();
    expect(snap.ip).toBe('');
    expect(['idle', 'disconnected']).toContain(snap.connection);
    expect(snap.status).toBeDefined();
    expect(snap.status?.effect).toBe('');
  });

  it('dashboard loads with saved node and initializes fleet state', () => {
    addSavedNode('192.168.1.100', 'Main Stage');
    setSelectedNodeIp('192.168.1.100');

    expect(getSelectedNodeIp()).toBe('192.168.1.100');
    const nodes = getSavedNodes();
    expect(nodes).toHaveLength(1);
    expect(nodes[0].name).toBe('Main Stage');
  });

  it('handles connect, disconnect, and reconnect transitions', async () => {
    await connect('192.168.1.100');
    await new Promise(r => setTimeout(r, 15));
    let snap = getNodeLiveSnapshot();
    expect(snap.ip).toBe('192.168.1.100');
    expect(['online', 'connected']).toContain(snap.connection);

    disconnect();
    snap = getNodeLiveSnapshot();
    expect(['disconnected', 'idle', 'offline']).toContain(snap.connection);

    await connect('192.168.1.100');
    await new Promise(r => setTimeout(r, 15));
    snap = getNodeLiveSnapshot();
    expect(['online', 'connected']).toContain(snap.connection);
  });

  it('succeeds with HTTP commands transmitting Bearer authentication', async () => {
    const fetchSpy = vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      new Response(JSON.stringify({ status: 'ok', data: 42 }), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      })
    );

    const result = await apiJson<{ status: string; data: number }>('192.168.1.100', '/api/v1/test');
    expect(result.status).toBe('ok');
    expect(result.data).toBe(42);

    expect(fetchSpy).toHaveBeenCalled();
    const [url, init] = fetchSpy.mock.calls[0];
    expect(url).toBe('http://192.168.1.100/api/v1/test');
    const authHeader = (init?.headers as Headers).get('Authorization');
    expect(authHeader).toBe('Bearer test-session-token-100');
  });

  it('displays and types HTTP 401 / 403 errors appropriately without crashing', async () => {
    vi.spyOn(globalThis, 'fetch')
      .mockResolvedValueOnce(
        new Response(JSON.stringify({ error: 'unauthorized' }), { status: 401, statusText: 'Unauthorized' })
      )
      .mockResolvedValueOnce(
        new Response(JSON.stringify({ error: 'forbidden' }), { status: 403, statusText: 'Forbidden' })
      );

    await expect(apiJson('192.168.1.100', '/api/v1/config')).rejects.toThrow('HTTP 401');
    await expect(apiJson('192.168.1.100', '/api/v1/config')).rejects.toThrow('HTTP 403');
  });

  it('handles malformed API JSON responses gracefully with informative error', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      new Response('<<<NOT JSON AT ALL>>>', {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      })
    );

    await expect(apiJson('192.168.1.100', '/api/v1/config')).rejects.toThrow('is not valid JSON');
  });

  it('handles malformed WebSocket messages without crashing live state', async () => {
    await connect('192.168.1.100');
    await new Promise(r => setTimeout(r, 15));

    const ws = MockWebSocket.instances[0];
    expect(ws).toBeDefined();

    // Calling message parser with invalid json or malformed blob does not crash
    expect(() => {
      ws.onmessage?.({ data: '{broken json:;' });
      ws.onmessage?.({ data: 'invalid text' });
    }).not.toThrow();

    const snap = getNodeLiveSnapshot();
    expect(snap.ip).toBe('192.168.1.100');
    expect(['online', 'connected']).toContain(snap.connection);
  });

  it('verifies scene loading and saving integration contracts', async () => {
    const mockPreset = createDefaultPreset(0, 'Opening Look');
    vi.spyOn(globalThis, 'fetch')
      .mockResolvedValueOnce(
        new Response(JSON.stringify([mockPreset]), { status: 200, headers: { 'Content-Type': 'application/json' } })
      )
      .mockResolvedValueOnce(
        new Response(JSON.stringify({ saved: true }), { status: 200, headers: { 'Content-Type': 'application/json' } })
      );

    const loaded = await getPresets('192.168.1.100');
    expect(loaded).toHaveLength(1);
    expect(loaded[0].name).toBe('Opening Look');

    const saveRes = await savePreset('192.168.1.100', mockPreset);
    expect(saveRes.saved).toBe(true);
  });

  it('verifies timeline loading and saving integration contracts', async () => {
    const mockSeq = createDefaultSequence();
    vi.spyOn(globalThis, 'fetch')
      .mockResolvedValueOnce(
        new Response(JSON.stringify(mockSeq), { status: 200, headers: { 'Content-Type': 'application/json' } })
      )
      .mockResolvedValueOnce(
        new Response(JSON.stringify({ saved: true }), { status: 200, headers: { 'Content-Type': 'application/json' } })
      );

    const loaded = await getSequence('192.168.1.100');
    expect(loaded.bpm).toBe(120);

    const saveRes = await saveSequence('192.168.1.100', mockSeq);
    expect(saveRes.saved).toBe(true);
  });

  it('verifies factory status loading and blackout emergency abort', async () => {
    vi.spyOn(globalThis, 'fetch')
      .mockResolvedValueOnce(
        new Response(
          JSON.stringify({
            testActive: false,
            effect: 'identity',
            brightness: 32,
            outputsEnabled: true,
            autoLoop: false,
            commandRejections: 0,
          }),
          { status: 200, headers: { 'Content-Type': 'application/json' } }
        )
      )
      .mockResolvedValueOnce(
        new Response(JSON.stringify({ accepted: true, action: 'blackout' }), {
          status: 200,
          headers: { 'Content-Type': 'application/json' },
        })
      );

    const status = await getFactoryStatus('192.168.1.100');
    expect(status.outputsEnabled).toBe(true);
    expect(status.effect).toBe('identity');

    const abortRes = await postFactoryAbort('192.168.1.100');
    expect(abortRes.accepted).toBe(true);
    expect(abortRes.action).toBe('blackout');
  });
});
