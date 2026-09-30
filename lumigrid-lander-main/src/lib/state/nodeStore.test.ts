import { describe, expect, it, beforeEach, afterEach, vi } from 'vitest';
import {
  connect,
  disconnect,
  reconnect,
  selectNode,
  refreshHealth,
  getNodeLiveSnapshot,
  getNodeLiveState,
  nodePixelStore,
  nodeFrameCount,
  getWsUrl,
  _resetForTesting,
  BUF_SIZE,
} from './nodeStore';
import { setSelectedNodeIp, getSelectedNodeIp } from '../auth/storage';
import { nodeAuthStore } from '../auth/nodeAuth';
import * as authApi from '../api/auth';

class MockWebSocket {
  static instances: MockWebSocket[] = [];
  readyState = 0;
  binaryType = 'blob';
  url: string;
  protocols?: string | string[];
  onopen: (() => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  onmessage: ((event: { data: unknown }) => void) | null = null;

  constructor(url: string, protocols?: string | string[]) {
    this.url = url;
    this.protocols = protocols;
    MockWebSocket.instances.push(this);
    // Automatic open for testing unless manually controlled
    setTimeout(() => {
      if (this.readyState === 0) {
        this.readyState = 1;
        this.onopen?.();
      }
    }, 0);
  }

  close() {
    this.readyState = 3;
    this.onclose?.();
  }

  triggerOpen() {
    this.readyState = 1;
    this.onopen?.();
  }

  triggerClose() {
    this.readyState = 3;
    this.onclose?.();
  }

  triggerError() {
    this.onerror?.();
  }

  triggerMessage(data: unknown) {
    this.onmessage?.({ data });
  }
}

describe('unified dashboard node live state and lifecycle', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    MockWebSocket.instances = [];
    vi.stubGlobal('WebSocket', MockWebSocket);
    nodeAuthStore.clearAllSessions();

    const testIps = ['192.168.1.50', '192.168.1.60', '192.168.1.75', '192.168.1.80', '192.168.1.10', '192.168.1.20', '192.168.1.100'];
    for (const ip of testIps) {
      nodeAuthStore.setSession({
        address: ip,
        nodeId: `node-${ip.replace(/\./g, '-')}`,
        role: 'operator',
        sessionToken: `tok-${ip}`,
        expiresAt: Date.now() + 3600000,
      });
    }

    vi.spyOn(authApi, 'createWsTicket').mockImplementation(async (ip: string) => {
      return { ticket: `ticket-${ip.replace(/\./g, '-')}`, expiresInMs: 15000 };
    });

    _resetForTesting();
  });

  afterEach(() => {
    _resetForTesting();
    nodeAuthStore.clearAllSessions();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
    vi.useRealTimers();
  });

  describe('WebSocket URL selection (HTTPS vs HTTP)', () => {
    it('generates ws:// URL under http protocol', () => {
      vi.stubGlobal('location', { protocol: 'http:' });
      expect(getWsUrl('192.168.1.50')).toBe('ws://192.168.1.50/ws');
    });

    it('strictly preserves ws:// URL even under https protocol (LG-031 0.6)', () => {
      vi.stubGlobal('location', { protocol: 'https:' });
      expect(getWsUrl('192.168.1.50')).toBe('ws://192.168.1.50/ws');
    });
  });

  describe('WebSocket Authentication & Tickets (LG-029)', () => {
    it('obtains one-time ticket before connect and passes in Sec-WebSocket-Protocol', async () => {
      const ticketSpy = vi.spyOn(authApi, 'createWsTicket');
      await connect('192.168.1.50');

      expect(ticketSpy).toHaveBeenCalledWith('192.168.1.50');
      expect(MockWebSocket.instances.length).toBe(1);
      expect(MockWebSocket.instances[0].protocols).toEqual(['lumigrid-ticket.ticket-192-168-1-50']);
    });

    it('rejects connect without active session and does not open socket', async () => {
      nodeAuthStore.clearSession('192.168.1.50');
      await connect('192.168.1.50');

      expect(MockWebSocket.instances.length).toBe(0);
      expect(['auth-required', 'disconnected']).toContain(getNodeLiveSnapshot().connection);
      expect(getNodeLiveSnapshot().error).toBe('Authentication required');
    });
  });

  describe('Connection and Reconnect lifecycle (Cases A - F)', () => {
    it('Case A: connect("192.168.1.50") connects to exactly that IP', async () => {
      await connect('192.168.1.50');
      expect(MockWebSocket.instances.length).toBe(1);
      expect(MockWebSocket.instances[0].url).toContain('192.168.1.50');
      expect(getNodeLiveSnapshot().nodeIp).toBe('192.168.1.50');
    });

    it('Case B: reconnect("192.168.1.60") disconnects existing socket and connects to new IP', async () => {
      await connect('192.168.1.50');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      await reconnect('192.168.1.60');
      expect(ws1.readyState).toBe(3); // closed
      expect(MockWebSocket.instances.length).toBe(2);
      expect(MockWebSocket.instances[1].url).toContain('192.168.1.60');
      expect(getNodeLiveSnapshot().nodeIp).toBe('192.168.1.60');
    });

    it('Case C: reconnect() with no args uses the currently selected node', async () => {
      await connect('192.168.1.75');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      await reconnect();
      expect(ws1.readyState).toBe(3);
      expect(MockWebSocket.instances.length).toBe(2);
      expect(MockWebSocket.instances[1].url).toContain('192.168.1.75');
    });

    it('Case D: unexpected socket close triggers automatic reconnect to same node', async () => {
      await connect('192.168.1.80');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      // Unexpected close
      ws1.triggerClose();
      expect(['offline', 'disconnected']).toContain(getNodeLiveSnapshot().connection);

      // Fast-forward reconnect timer
      await vi.advanceTimersByTimeAsync(600);
      expect(MockWebSocket.instances.length).toBe(2);
      expect(MockWebSocket.instances[1].url).toContain('192.168.1.80');
    });

    it('Case E: switching node prevents old socket events from corrupting new node', async () => {
      await connect('192.168.1.10');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      // Switch to node 2
      await connect('192.168.1.20');
      const ws2 = MockWebSocket.instances[1];
      ws2.triggerOpen();

      expect(getNodeLiveSnapshot().nodeIp).toBe('192.168.1.20');

      // Stale message arrives from old socket ws1
      ws1.triggerMessage(JSON.stringify({ effect: 'stale_effect', brightness: 10 }));
      expect(getNodeLiveSnapshot().status?.effect).not.toBe('stale_effect');

      // Stale close from old socket does not trigger reconnect for node 1
      ws1.triggerClose();
      await vi.advanceTimersByTimeAsync(2000);
      expect(MockWebSocket.instances.length).toBe(2); // no extra reconnect spawned
    });

    it('Case F: explicit disconnect disables automatic reconnect', async () => {
      await connect('192.168.1.50');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      disconnect();
      expect(['offline', 'disconnected']).toContain(getNodeLiveSnapshot().connection);

      // Advancing time should not trigger any reconnect
      await vi.advanceTimersByTimeAsync(30000);
      expect(MockWebSocket.instances.length).toBe(1);
    });
  });

  describe('Protocol message handling & validation', () => {
    it('updates status on valid status frame with recognized fields', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      ws.triggerMessage(JSON.stringify({
        effect: 'fire',
        brightness: 200,
        outputsEnabled: true,
        testActive: false,
        autoLoop: false,
      }));

      const snap = getNodeLiveSnapshot();
      expect(snap.status?.effect).toBe('fire');
      expect(snap.status?.brightness).toBe(200);
      expect(snap.status?.outputsEnabled).toBe(true);
      expect(snap.lastMessageAt).toBeGreaterThan(0);
    });

    it('handles partial status message without erasing other fields', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      ws.triggerMessage(JSON.stringify({ effect: 'aurora', brightness: 150 }));
      ws.triggerMessage(JSON.stringify({ brightness: 180 }));

      const snap = getNodeLiveSnapshot();
      expect(snap.status?.effect).toBe('aurora');
      expect(snap.status?.brightness).toBe(180);
    });

    it('parses schema2 status frame with per-channel owner and manual override block', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      const schema2Payload = {
        schema: 2,
        seq: 1,
        nodeId: 'node-test-01',
        uptimeMs: 12345,
        channels: [
          { index: 0, port: 'A1', configured: true, enabled: true, owner: 'ManualOverride', fault: false, brightness: 255, pixels: 60 },
          { index: 1, port: 'A2', configured: true, enabled: true, owner: 'ScenePlayback', fault: false, brightness: 255, pixels: 60 },
        ],
        manual: {
          active: true,
          channelMask: 1,
          leaseRemainingMs: 14200,
        },
        state: {
          runtime: { effect: 'solid', brightness: 255, testActive: false, autoLoop: false, controlOwner: 'ManualOverride', frameCount: 100 },
          output: { outputsEnabled: true, powerMw: 12000, fault: false, faultCount: 0 },
          manual: { active: true, channelMask: 1, leaseRemainingMs: 14200 },
        },
      };

      ws.triggerMessage(JSON.stringify(schema2Payload));

      const snap = getNodeLiveSnapshot();
      expect(snap.status?.channels?.[0]?.owner).toBe('ManualOverride');
      expect(snap.status?.channels?.[1]?.owner).toBe('ScenePlayback');
      expect(snap.status?.manual).toEqual({
        active: true,
        channelMask: 1,
        leaseRemainingMs: 14200,
      });
    });

    it('ignores unrelated JSON objects without status fields', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      ws.triggerMessage(JSON.stringify({ effect: 'sparkle', brightness: 120 }));
      const msgAt = getNodeLiveSnapshot().lastMessageAt;

      vi.advanceTimersByTime(100);
      ws.triggerMessage(JSON.stringify({ unrelated: true, ping: 'pong' }));

      const snap = getNodeLiveSnapshot();
      expect(snap.status?.effect).toBe('sparkle');
      expect(snap.lastMessageAt).toBe(msgAt); // unchanged
    });

    it('defensively ignores malformed JSON', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      ws.triggerMessage('{ incomplete json ');
      expect(['online', 'connected']).toContain(getNodeLiveSnapshot().connection);
    });

    it('processes valid binary pixel frame with header 1', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      const frame = new Uint8Array([1, 100, 150, 200]);
      ws.triggerMessage(frame.buffer);

      const snap = getNodeLiveSnapshot();
      expect(snap.frameCount).toBe(1);
      expect(snap.pixels[0]).toBe(100);
      expect(snap.pixels[1]).toBe(150);
      expect(snap.pixels[2]).toBe(200);
    });

    it('ignores binary frames with unrecognized header byte', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      const frame = new Uint8Array([99, 100, 150, 200]);
      ws.triggerMessage(frame.buffer);

      const snap = getNodeLiveSnapshot();
      expect(snap.frameCount).toBe(0);
      expect(snap.pixels[0]).toBe(0);
    });

    it('handles oversized binary pixel frames gracefully', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      // Buffer larger than BUF_SIZE + 1
      const big = new Uint8Array(BUF_SIZE + 500);
      big[0] = 1;
      big[1] = 42;
      ws.triggerMessage(big.buffer);

      const snap = getNodeLiveSnapshot();
      expect(snap.frameCount).toBe(1);
      expect(snap.pixels[0]).toBe(42);
      expect(snap.pixels.length).toBe(BUF_SIZE);
    });
  });

  describe('Health polling and failure reporting', () => {
    it('cleans up previous poll timer and maintains exactly one interval', async () => {
      const setIntervalSpy = vi.spyOn(globalThis, 'setInterval');
      const clearIntervalSpy = vi.spyOn(globalThis, 'clearInterval');

      await connect('192.168.1.50');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      expect(setIntervalSpy).toHaveBeenCalledTimes(1);

      // Reconnect to same or different node
      await reconnect('192.168.1.60');
      const ws2 = MockWebSocket.instances[1];
      ws2.triggerOpen();

      expect(clearIntervalSpy).toHaveBeenCalled();
      disconnect();
      expect(clearIntervalSpy).toHaveBeenCalled();
    });

    it('preserves existing health snapshot and populates healthError on poll failure', async () => {
      const mockHealth = {
        schema_version: 1,
        system: { uptime_s: 100, free_heap: 50000, min_free_heap: 40000, reset_reason: 1, fps: 60 },
        network: { wifi_rssi: -50 },
        nvs: { used: 10, free: 90 },
        config: { valid: true, error_count: 0, first_error: '', persistence_state: 'saved', primary_valid: true, backup_valid: true },
        startup: { state: 'ok', stage: 'app', failing_stage: '', failing_error: 0 },
        output: { state: 'running', fault: false, fault_events: 0, failed_commits: 0, last_error: 0, estop_latched: false, runtime_estop: false, control_source: 1, live_objects: 1, current_power_mw: 500, throttling_events: 0, active_preset: 0, watchdog_fallback: false },
        dmx: { sources_active: 0, universes_active: 0, sources_dropped: 0, packets_accepted: 0, packets_rejected_seq: 0, packets_ignored_unpatched: 0, submission_failures: 0, configured_universes: [] },
        sync: { packet_loss: 0 },
      };

      vi.stubGlobal('fetch', vi.fn().mockResolvedValueOnce(
        new Response(JSON.stringify(mockHealth), { status: 200 })
      ));

      await refreshHealth('192.168.1.50');
      expect(getNodeLiveSnapshot().health).toEqual(mockHealth);
      expect(getNodeLiveSnapshot().healthError).toBe('');

      // Subsequent failure
      vi.stubGlobal('fetch', vi.fn().mockRejectedValueOnce(new Error('Connection refused')));
      await refreshHealth('192.168.1.50');

      const snap = getNodeLiveSnapshot();
      expect(snap.health).toEqual(mockHealth); // NOT wiped out
      expect(snap.healthError).toContain('Connection refused');
      expect(snap.healthUpdatedAt).toBeGreaterThan(0);
    });
  });

  describe('WebSocket Schema 2 ingestion and sequence tracking', () => {
    it('parses Schema 2 snapshot and populates extended state', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      const schema2Msg = {
        schema: 2,
        seq: 100,
        nodeId: 'lg-84f703c29110',
        uptimeMs: 123456,
        state: {
          runtime: {
            effect: 'rainbow',
            brightness: 80,
            testActive: false,
            autoLoop: true,
            controlOwner: 'ScenePlayback',
            frameCount: 5400,
          },
          output: {
            outputsEnabled: true,
            powerMw: 1250,
            fault: false,
            faultCount: 0,
          },
          scene: {
            activePreset: 3,
            requestedPreset: 3,
            requestedGeneration: 42,
            appliedGeneration: 42,
            sceneApplyPending: false,
            transitioning: false,
          },
          sync: {
            role: 'master',
            inSync: true,
          },
          network: {
            rssi: -58,
          },
          protocol: {
            artnet: { enabled: true, sources: 1, packetsPerSecond: 44, lastPacketAgeMs: 12 },
            sacn: { enabled: true, sources: 0, packetsPerSecond: 0 },
            owner: 'ScenePlayback',
          },
          job: {
            jobId: 10,
            type: 'ota',
            state: 'writing',
            progress: 55,
            startedMs: 120000,
            error: null,
          },
        },
      };

      ws.triggerMessage(JSON.stringify(schema2Msg));

      const snap = getNodeLiveSnapshot();
      expect(snap.nodeId).toBe('lg-84f703c29110');
      expect(snap.status?.effect).toBe('rainbow');
      expect(snap.status?.brightness).toBe(80);
      expect(snap.status?.activePreset).toBe(3);
      expect(snap.status?.appliedGeneration).toBe(42);
      expect(snap.status?.requestedGeneration).toBe(42);
      expect(snap.status?.controlOwner).toBe('ScenePlayback');
      expect(snap.status?.powerMw).toBe(1250);
      expect(snap.status?.seq).toBe(100);
      expect(snap.status?.protocol?.artnet?.packetsPerSecond).toBe(44);
      expect(snap.status?.job?.progress).toBe(55);
    });

    it('detects dropped sequence numbers and transitions connection to degraded', async () => {
      await connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      ws.triggerMessage(JSON.stringify({
        schema: 2,
        seq: 1,
        nodeId: 'lg-test',
        uptimeMs: 1000,
        state: { runtime: {}, output: {}, scene: { appliedGeneration: 1 } },
      }));
      expect(['online', 'connected']).toContain(getNodeLiveSnapshot().connection);

      // Sequence gap: jumps from 1 to 5
      ws.triggerMessage(JSON.stringify({
        schema: 2,
        seq: 5,
        nodeId: 'lg-test',
        uptimeMs: 2000,
        state: { runtime: {}, output: {}, scene: { appliedGeneration: 2 } },
      }));
      expect(getNodeLiveSnapshot().connection).toBe('degraded');

      // Subsequent consecutive sequence (6 after 5) heals to connected
      ws.triggerMessage(JSON.stringify({
        schema: 2,
        seq: 6,
        nodeId: 'lg-test',
        uptimeMs: 3000,
        state: { runtime: {}, output: {}, scene: { appliedGeneration: 3 } },
      }));
      expect(['online', 'connected']).toContain(getNodeLiveSnapshot().connection);
    });
  });
});
