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
    _resetForTesting();
  });

  afterEach(() => {
    _resetForTesting();
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  describe('WebSocket URL selection (HTTPS vs HTTP)', () => {
    it('generates ws:// URL under http protocol', () => {
      vi.stubGlobal('location', { protocol: 'http:' });
      expect(getWsUrl('192.168.1.50')).toBe('ws://192.168.1.50/ws');
    });

    it('generates wss:// URL under https protocol', () => {
      vi.stubGlobal('location', { protocol: 'https:' });
      expect(getWsUrl('192.168.1.50')).toBe('wss://192.168.1.50/ws');
    });
  });

  describe('Connection and Reconnect lifecycle (Cases A - F)', () => {
    it('Case A: connect("192.168.1.50") connects to exactly that IP', () => {
      connect('192.168.1.50');
      expect(MockWebSocket.instances.length).toBe(1);
      expect(MockWebSocket.instances[0].url).toContain('192.168.1.50');
      expect(getNodeLiveSnapshot().nodeIp).toBe('192.168.1.50');
    });

    it('Case B: reconnect("192.168.1.60") disconnects existing socket and connects to new IP', () => {
      connect('192.168.1.50');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      reconnect('192.168.1.60');
      expect(ws1.readyState).toBe(3); // closed
      expect(MockWebSocket.instances.length).toBe(2);
      expect(MockWebSocket.instances[1].url).toContain('192.168.1.60');
      expect(getNodeLiveSnapshot().nodeIp).toBe('192.168.1.60');
    });

    it('Case C: reconnect() with no args uses the currently selected node', () => {
      connect('192.168.1.75');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      reconnect();
      expect(ws1.readyState).toBe(3);
      expect(MockWebSocket.instances.length).toBe(2);
      expect(MockWebSocket.instances[1].url).toContain('192.168.1.75');
    });

    it('Case D: unexpected socket close triggers automatic reconnect to same node', () => {
      connect('192.168.1.80');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      // Unexpected close
      ws1.triggerClose();
      expect(getNodeLiveSnapshot().connection).toBe('disconnected');

      // Fast-forward reconnect timer
      vi.advanceTimersByTime(600);
      expect(MockWebSocket.instances.length).toBe(2);
      expect(MockWebSocket.instances[1].url).toContain('192.168.1.80');
    });

    it('Case E: switching node prevents old socket events from corrupting new node', () => {
      connect('192.168.1.10');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      // Switch to node 2
      connect('192.168.1.20');
      const ws2 = MockWebSocket.instances[1];
      ws2.triggerOpen();

      expect(getNodeLiveSnapshot().nodeIp).toBe('192.168.1.20');

      // Stale message arrives from old socket ws1
      ws1.triggerMessage(JSON.stringify({ effect: 'stale_effect', brightness: 10 }));
      expect(getNodeLiveSnapshot().status?.effect).not.toBe('stale_effect');

      // Stale close from old socket does not trigger reconnect for node 1
      ws1.triggerClose();
      vi.advanceTimersByTime(2000);
      expect(MockWebSocket.instances.length).toBe(2); // no extra reconnect spawned
    });

    it('Case F: explicit disconnect disables automatic reconnect', () => {
      connect('192.168.1.50');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      disconnect();
      expect(getNodeLiveSnapshot().connection).toBe('disconnected');

      // Advancing time should not trigger any reconnect
      vi.advanceTimersByTime(30000);
      expect(MockWebSocket.instances.length).toBe(1);
    });
  });

  describe('Protocol message handling & validation', () => {
    it('updates status on valid status frame with recognized fields', () => {
      connect('192.168.1.50');
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

    it('handles partial status message without erasing other fields', () => {
      connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      ws.triggerMessage(JSON.stringify({ effect: 'aurora', brightness: 150 }));
      ws.triggerMessage(JSON.stringify({ brightness: 180 }));

      const snap = getNodeLiveSnapshot();
      expect(snap.status?.effect).toBe('aurora');
      expect(snap.status?.brightness).toBe(180);
    });

    it('ignores unrelated JSON objects without status fields', () => {
      connect('192.168.1.50');
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

    it('defensively ignores malformed JSON', () => {
      connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      ws.triggerMessage('{ incomplete json ');
      expect(getNodeLiveSnapshot().connection).toBe('connected');
    });

    it('processes valid binary pixel frame with header 1', () => {
      connect('192.168.1.50');
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

    it('ignores binary frames with unrecognized header byte', () => {
      connect('192.168.1.50');
      const ws = MockWebSocket.instances[0];
      ws.triggerOpen();

      const frame = new Uint8Array([99, 100, 150, 200]);
      ws.triggerMessage(frame.buffer);

      const snap = getNodeLiveSnapshot();
      expect(snap.frameCount).toBe(0);
      expect(snap.pixels[0]).toBe(0);
    });

    it('handles oversized binary pixel frames gracefully', () => {
      connect('192.168.1.50');
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
    it('cleans up previous poll timer and maintains exactly one interval', () => {
      const setIntervalSpy = vi.spyOn(globalThis, 'setInterval');
      const clearIntervalSpy = vi.spyOn(globalThis, 'clearInterval');

      connect('192.168.1.50');
      const ws1 = MockWebSocket.instances[0];
      ws1.triggerOpen();

      expect(setIntervalSpy).toHaveBeenCalledTimes(1);

      // Reconnect to same or different node
      reconnect('192.168.1.60');
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
});
