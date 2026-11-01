import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { nodeAuthStore } from '../auth/nodeAuth';
import * as authApi from '../api/auth';
import * as nodeApi from '../api/node';
import { normalizeRuntimeSnapshot } from './compat/runtimeSnapshotV2';
import {
  _resetForTesting, connect, disconnect, getNodeLiveSnapshot,
  recoverStateViaHttp, refreshHealth,
} from './nodeStore';

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

class Socket {
  static OPEN = 1;
  static CONNECTING = 0;
  static instances: Socket[] = [];
  readyState = 0;
  binaryType = '';
  onopen: (() => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  onmessage: ((event: { data: unknown }) => void) | null = null;
  constructor(public url: string, public protocols: string[]) { Socket.instances.push(this); }
  close() { this.readyState = 3; this.onclose?.(); }
  open() { this.readyState = 1; this.onopen?.(); }
  message(data: unknown) { this.onmessage?.({ data }); }
}

const a = '192.168.1.50';
const b = '192.168.1.60';
function session(ip: string, token = ip) {
  nodeAuthStore.setSession({ address: ip, role: 'admin', sessionToken: token, expiresAt: Date.now() + 60000 });
}
function snapshot(effect: string) {
  return normalizeRuntimeSnapshot({ effect, brightness: 100, outputsEnabled: true });
}

describe('focused-node async ownership', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.stubGlobal('WebSocket', Socket);
    _resetForTesting();
    Socket.instances = [];
    nodeAuthStore.clearAllSessions();
    session(a); session(b);
    vi.spyOn(authApi, 'createWsTicket').mockResolvedValue({ ticket: 'test', expiresInMs: 15000 });
    vi.spyOn(nodeApi, 'getStatus').mockImplementation(() => new Promise(() => {}));
    vi.spyOn(nodeApi, 'getHealth').mockImplementation(() => new Promise(() => {}));
  });
  afterEach(() => {
    _resetForTesting();
    nodeAuthStore.clearAllSessions();
    vi.restoreAllMocks();
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  it('discards HTTP status from a node abandoned while the body was pending', async () => {
    await connect(a);
    const pending = deferred<ReturnType<typeof snapshot>>();
    vi.mocked(nodeApi.getStatus).mockReturnValueOnce(pending.promise);
    const recovery = recoverStateViaHttp(a);
    await connect(b);
    Socket.instances.at(-1)!.message(JSON.stringify({ effect: 'node-b', brightness: 40 }));
    pending.resolve(snapshot('node-a'));
    await recovery;
    expect(getNodeLiveSnapshot().status?.effect).toBe('node-b');
  });

  it('discards old health and old failure after switching nodes', async () => {
    await connect(a);
    const pending = deferred<nodeApi.HealthSnapshot>();
    vi.mocked(nodeApi.getHealth).mockReturnValueOnce(pending.promise);
    const health = refreshHealth(a);
    await connect(b);
    pending.resolve({ system: { uptime_s: 123 } } as nodeApi.HealthSnapshot);
    await health;
    expect(getNodeLiveSnapshot().health).toBeNull();
    expect(getNodeLiveSnapshot().healthUpdatedAt).toBe(0);
  });

  it('rejects the first ticket after A → B → A', async () => {
    const pending = deferred<{ ticket: string; expiresInMs: number }>();
    vi.mocked(authApi.createWsTicket).mockReturnValueOnce(pending.promise);
    const first = connect(a);
    await connect(b);
    await connect(a);
    const currentSocket = Socket.instances.at(-1);
    pending.resolve({ ticket: 'obsolete', expiresInMs: 15000 });
    await first;
    expect(Socket.instances).toHaveLength(2);
    expect(Socket.instances.at(-1)).toBe(currentSocket);
  });

  it('rejects a ticket issued under a replaced session on the same node', async () => {
    const pending = deferred<{ ticket: string; expiresInMs: number }>();
    vi.mocked(authApi.createWsTicket).mockReturnValueOnce(pending.promise);
    const first = connect(a);
    session(a, 'new-session');
    pending.resolve({ ticket: 'old-session-ticket', expiresInMs: 15000 });
    await first;
    expect(Socket.instances).toHaveLength(0);
  });

  it('clears node-specific telemetry and preview even when the next node needs sign-in', async () => {
    await connect(a);
    const socket = Socket.instances[0];
    socket.message(JSON.stringify({ effect: 'node-a', brightness: 240 }));
    socket.message(new Uint8Array([2, 1, 1, 0, 250, 10, 10]).buffer);
    nodeAuthStore.clearSession(b);
    await connect(b);
    const current = getNodeLiveSnapshot();
    expect(current.snapshot).toBeNull();
    expect(current.status).toBeNull();
    expect(current.lastSnapshotAt).toBe(0);
    expect(current.pixels.every(value => value === 0)).toBe(true);
  });

  it('does not refresh authoritative status age when preview frames keep arriving', async () => {
    await connect(a);
    const socket = Socket.instances[0];
    socket.message(JSON.stringify({ effect: 'node-a', brightness: 100 }));
    const observedAt = getNodeLiveSnapshot().lastSnapshotAt;
    await vi.advanceTimersByTimeAsync(5000);
    socket.message(new Uint8Array([2, 1, 1, 0, 10, 20, 30]).buffer);
    expect(getNodeLiveSnapshot().lastSnapshotAt).toBe(observedAt);
    expect(getNodeLiveSnapshot().lastMessageAt).toBeGreaterThan(observedAt);
  });

  it('does not roll a newer stream snapshot back to an earlier HTTP result', async () => {
    await connect(a);
    const pending = deferred<ReturnType<typeof snapshot>>();
    vi.mocked(nodeApi.getStatus).mockReturnValueOnce(pending.promise);
    const recovery = recoverStateViaHttp(a);
    Socket.instances[0].message(JSON.stringify({ effect: 'newer-stream', brightness: 90 }));
    pending.resolve(snapshot('older-http'));
    await recovery;
    expect(getNodeLiveSnapshot().status?.effect).toBe('newer-stream');
  });

  it('does not publish a late HTTP result after an explicit disconnect', async () => {
    await connect(a);
    const pending = deferred<ReturnType<typeof snapshot>>();
    vi.mocked(nodeApi.getStatus).mockReturnValueOnce(pending.promise);
    const recovery = recoverStateViaHttp(a);
    disconnect();
    pending.resolve(snapshot('abandoned'));
    await recovery;
    expect(getNodeLiveSnapshot().snapshot).toBeNull();
  });
});
