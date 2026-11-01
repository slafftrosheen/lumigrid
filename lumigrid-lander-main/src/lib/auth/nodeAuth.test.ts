import { describe, expect, it, beforeEach, afterEach, vi } from 'vitest';
import { nodeAuthStore } from './nodeAuth';
import * as storageModule from './storage';
import * as transportModule from '../api/transport';
import { apiJson } from '../api/node';
import { saveNetworkConfig } from '../api/config';
import { uploadFirmware } from '../api/firmware';

class MockStorage implements Storage {
  private store: Record<string, string> = {};
  get length() {
    return Object.keys(this.store).length;
  }
  clear() {
    this.store = {};
  }
  getItem(key: string): string | null {
    return this.store[key] ?? null;
  }
  key(index: number): string | null {
    return Object.keys(this.store)[index] ?? null;
  }
  removeItem(key: string): void {
    delete this.store[key];
  }
  setItem(key: string, value: string): void {
    this.store[key] = String(value);
  }
}

describe('node-authoritative auth & session invariants', () => {
  let mockLocal: MockStorage;
  let mockSession: MockStorage;

  beforeEach(() => {
    mockLocal = new MockStorage();
    mockSession = new MockStorage();
    vi.stubGlobal('localStorage', mockLocal);
    vi.stubGlobal('sessionStorage', mockSession);
    nodeAuthStore.clearAllSessions();
  });

  afterEach(() => {
    nodeAuthStore.clearAllSessions();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it('proves legacy PIN and token fallback symbols do not exist', () => {
    expect((storageModule as Record<string, unknown>).DEFAULT_OP_PIN).toBeUndefined();
    expect((storageModule as Record<string, unknown>).DEFAULT_AD_PIN).toBeUndefined();
    expect((storageModule as Record<string, unknown>).OPERATOR_HASH_KEY).toBeUndefined();
    expect((storageModule as Record<string, unknown>).ADMIN_HASH_KEY).toBeUndefined();
    expect((storageModule as Record<string, unknown>).AUTH_TIMESTAMP_KEY).toBeUndefined();
    expect((transportModule as Record<string, unknown>).DEV_API_TOKEN).toBeUndefined();
  });

  it('stores session tokens only in memory and sessionStorage, NEVER in localStorage', () => {
    nodeAuthStore.setSession({
      address: '192.168.1.100',
      nodeId: 'node-100',
      role: 'admin',
      sessionToken: 'secret-session-token-xyz',
      expiresAt: Date.now() + 3600000,
    });

    // Check localStorage contains NO token or secret
    for (let i = 0; i < mockLocal.length; i++) {
      const key = mockLocal.key(i);
      if (key) {
        expect(mockLocal.getItem(key)).not.toContain('secret-session-token-xyz');
      }
    }

    // Check sessionStorage contains the session table
    const stored = mockSession.getItem('lg_node_sessions');
    expect(stored).toBeDefined();
    expect(stored).toContain('secret-session-token-xyz');
  });

  it('restores sessions from sessionStorage on tab reload', () => {
    mockSession.setItem(
      'lg_node_sessions',
      JSON.stringify([
        {
          address: '192.168.1.100',
          nodeId: 'node-100',
          role: 'installer',
          sessionToken: 'persisted-session-123',
          expiresAt: Date.now() + 60000,
        },
      ])
    );

    // Call restoreFromStorage
    nodeAuthStore.restoreFromStorage();

    const session = nodeAuthStore.getSession('192.168.1.100');
    expect(session).toBeDefined();
    expect(session?.role).toBe('installer');
    expect(session?.sessionToken).toBe('persisted-session-123');
  });

  it('keeps an in-memory session usable when the tab storage getter is denied', () => {
    vi.stubGlobal('window', Object.defineProperty({}, 'sessionStorage', {
      get() { throw new Error('Storage denied'); },
    }));
    expect(() => nodeAuthStore.setSession({
      address: '192.168.1.100',
      role: 'operator',
      sessionToken: 'memory-only-token',
      expiresAt: Date.now() + 60000,
    })).not.toThrow();
    expect(nodeAuthStore.getSession('192.168.1.100')?.sessionToken).toBe('memory-only-token');
    expect(mockLocal.length).toBe(0);
    expect(mockSession.length).toBe(0);
    expect(() => nodeAuthStore.clearAllSessions()).not.toThrow();
    expect(nodeAuthStore.getSession('192.168.1.100')).toBeNull();
  });

  it('initializes the auth module when tab persistence is disabled', async () => {
    vi.stubGlobal('window', Object.defineProperty({}, 'sessionStorage', {
      get() { throw new Error('Storage denied'); },
    }));
    vi.resetModules();
    const restored = await import('./nodeAuth');
    expect(restored.nodeAuthStore.getSession('192.168.1.100')).toBeNull();
  });

  it('restores a tab session during fresh module initialization', async () => {
    mockSession.setItem('lg_node_sessions', JSON.stringify([{
      address: '192.168.1.100',
      nodeId: 'node-100',
      role: 'operator',
      sessionToken: 'initial-tab-token',
      expiresAt: Date.now() + 60000,
    }]));
    vi.resetModules();
    const restored = await import('./nodeAuth');
    expect(restored.nodeAuthStore.getSession('192.168.1.100')?.sessionToken).toBe('initial-tab-token');
    expect(mockLocal.length).toBe(0);
  });

  it('discards expired sessions on restoreFromStorage', () => {
    mockSession.setItem(
      'lg_node_sessions',
      JSON.stringify([
        {
          address: '192.168.1.100',
          nodeId: 'node-100',
          role: 'operator',
          sessionToken: 'expired-session-456',
          expiresAt: Date.now() - 5000,
        },
      ])
    );

    nodeAuthStore.restoreFromStorage();

    const session = nodeAuthStore.getSession('192.168.1.100');
    expect(session).toBeNull();
  });

  it('maintains independent session states across multiple fleet nodes', () => {
    nodeAuthStore.setSession({
      address: '192.168.1.10',
      nodeId: 'node-10',
      role: 'admin',
      sessionToken: 'token-admin-10',
      expiresAt: Date.now() + 3600000,
    });
    nodeAuthStore.setSession({
      address: '192.168.1.20',
      nodeId: 'node-20',
      role: 'operator',
      sessionToken: 'token-op-20',
      expiresAt: Date.now() + 3600000,
    });

    expect(nodeAuthStore.getSession('192.168.1.10')?.role).toBe('admin');
    expect(nodeAuthStore.getSession('192.168.1.10')?.sessionToken).toBe('token-admin-10');
    expect(nodeAuthStore.getSession('192.168.1.20')?.role).toBe('operator');
    expect(nodeAuthStore.getSession('192.168.1.20')?.sessionToken).toBe('token-op-20');
  });

  it('clears only the selected node session on 401 error', async () => {
    nodeAuthStore.setSession({
      address: '192.168.1.10',
      nodeId: 'node-10',
      role: 'admin',
      sessionToken: 'token-10',
      expiresAt: Date.now() + 3600000,
    });
    nodeAuthStore.setSession({
      address: '192.168.1.20',
      nodeId: 'node-20',
      role: 'admin',
      sessionToken: 'token-20',
      expiresAt: Date.now() + 3600000,
    });

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
      new Response(JSON.stringify({ error: 'session_expired' }), {
        status: 401,
        statusText: 'Unauthorized',
        headers: { 'Content-Type': 'application/json' },
      })
    );

    await expect(apiJson('192.168.1.10', '/api/v1/config')).rejects.toThrow('HTTP 401');

    // Node 10 session must be cleared
    expect(nodeAuthStore.getSession('192.168.1.10')).toBeNull();

    // Node 20 session must be RETAINED
    expect(nodeAuthStore.getSession('192.168.1.20')).toBeDefined();
    expect(nodeAuthStore.getSession('192.168.1.20')?.sessionToken).toBe('token-20');
  });

  it('retains session on 403 forbidden error', async () => {
    nodeAuthStore.setSession({
      address: '192.168.1.10',
      nodeId: 'node-10',
      role: 'operator',
      sessionToken: 'token-op-10',
      expiresAt: Date.now() + 3600000,
    });

    vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
      new Response(JSON.stringify({ error: 'insufficient_role' }), {
        status: 403,
        statusText: 'Forbidden',
        headers: { 'Content-Type': 'application/json' },
      })
    );

    await expect(apiJson('192.168.1.10', '/api/v1/config')).rejects.toThrow('HTTP 403');

    // Session is still active on 403
    expect(nodeAuthStore.getSession('192.168.1.10')).toBeDefined();
    expect(nodeAuthStore.getSession('192.168.1.10')?.role).toBe('operator');
  });

  it('clears session on explicit logout', () => {
    nodeAuthStore.setSession({
      address: '192.168.1.10',
      nodeId: 'node-10',
      role: 'admin',
      sessionToken: 'token-admin-10',
      expiresAt: Date.now() + 3600000,
    });

    nodeAuthStore.clearSession('192.168.1.10');
    expect(nodeAuthStore.getSession('192.168.1.10')).toBeNull();
  });

  it('uses node bearer session for network configuration writes', async () => {
    nodeAuthStore.setSession({
      address: '192.168.1.10',
      nodeId: 'node-10',
      role: 'admin',
      sessionToken: 'admin-sess-token-42',
      expiresAt: Date.now() + 3600000,
    });

    const fetchSpy = vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
      new Response(JSON.stringify({ saved: true }), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      })
    );

    await saveNetworkConfig('192.168.1.10', {
      mode: 'sta_only',
      ssid: 'StudioNet',
    });

    expect(fetchSpy).toHaveBeenCalled();
    const [, init] = fetchSpy.mock.calls[0];
    const authHeader = (init?.headers as Headers).get('Authorization');
    expect(authHeader).toBe('Bearer admin-sess-token-42');
  });

  it('uses node bearer session for firmware OTA uploads without universal fallback', async () => {
    nodeAuthStore.setSession({
      address: '192.168.1.10',
      nodeId: 'node-10',
      role: 'admin',
      sessionToken: 'ota-sess-token-99',
      expiresAt: Date.now() + 3600000,
    });

    // Mock XMLHttpRequest
    const xhrHeaders: Record<string, string> = {};
    const mockXhr = {
      open: vi.fn(),
      setRequestHeader: vi.fn((k: string, v: string) => {
        xhrHeaders[k] = v;
      }),
      send: vi.fn(function (this: { onload?: () => void }) {
        setTimeout(() => {
          this.onload?.();
        }, 0);
      }),
      upload: {
        addEventListener: vi.fn(),
      },
      status: 200,
      responseText: JSON.stringify({ status: 'ok', accepted: true, commandId: 1, jobId: 1 }),
      readyState: 4,
    };

    vi.stubGlobal('XMLHttpRequest', function (this: unknown) {
      return Object.assign(this as object, mockXhr);
    });

    const dummyFirmware = new Blob(['fake binary firmware']);
    await uploadFirmware('192.168.1.10', dummyFirmware);

    expect(xhrHeaders['Authorization']).toBe('Bearer ota-sess-token-99');
    expect(xhrHeaders['X-Auth-Token']).toBeUndefined();
  });
});
