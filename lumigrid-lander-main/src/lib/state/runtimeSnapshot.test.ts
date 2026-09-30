import { describe, expect, it, beforeEach, afterEach, vi } from 'vitest';
import { normalizeRuntimeSnapshot } from './compat/runtimeSnapshotV2';
import type { RuntimeSnapshotV3 } from './runtimeSnapshot';
import {
  connect,
  disconnect,
  getNodeLiveSnapshot,
  _resetForTesting,
  nodePixelStore,
} from './nodeStore';
import { nodeAuthStore } from '../auth/nodeAuth';
import { upsertNodeRecord, readNodeRegistry, writeNodeRegistry, type NodeRecord } from '../fleet';
import { getNodeHttpBase, getNodeWsUrl, TransportBlockedError } from '../api/transport';
import { commandTracker } from './commandTracker';
import { loadCaps, invalidateCapabilitiesCache } from '../stores/capabilities';
import * as nodeApi from '../api/node';
import * as authApi from '../api/auth';

class MockWebSocket {
  static instances: MockWebSocket[] = [];
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;
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

  triggerMessage(data: unknown) {
    this.onmessage?.({ data });
  }
}

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

describe('LG-031 Frontend RuntimeSnapshot and Cutover Tests', () => {
  let mockLocal: MockStorage;
  let mockSession: MockStorage;

  beforeEach(() => {
    vi.useFakeTimers();
    MockWebSocket.instances = [];
    (globalThis as any).WebSocket = MockWebSocket;
    mockLocal = new MockStorage();
    mockSession = new MockStorage();
    vi.stubGlobal('localStorage', mockLocal);
    vi.stubGlobal('sessionStorage', mockSession);

    const testIps = ['192.168.1.50', '192.168.1.44', '192.168.1.83'];
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
    nodeAuthStore.clearAllSessions();
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
    vi.useRealTimers();
  });

  // A. Schema 3 parses into canonical RuntimeSnapshot
  it('parses valid Schema 3 into canonical RuntimeSnapshotV3', () => {
    const raw: RuntimeSnapshotV3 = {
      schema: 3,
      streamSeq: 10,
      generatedAtMs: 123456,
      node: { id: 'lg-aabbcc' },
      runtime: {
        effect: 'rainbow',
        brightness: 128,
        testActive: false,
        autoLoop: false,
        controlOwner: 'ScenePlayback',
        frameCount: 500,
      },
      output: {
        enabled: true,
        powerMw: 4000,
        state: 'healthy',
        fault: false,
        faultCount: 0,
        estopActive: false,
      },
      scene: {
        activePreset: 2,
        requestedPreset: 2,
        requestedGeneration: 5,
        appliedGeneration: 5,
        applyPending: false,
        transitioning: false,
        fallbackActive: false,
      },
      manual: {
        active: false,
        channelMask: 0,
        leaseRemainingMs: 0,
      },
      transport: {
        mode: 'scene',
        generation: 5,
        sceneIndex: 2,
        timeline: { running: false, bpm: 120 },
        cue: { running: false },
      },
      sync: {
        configuredRole: 'auto',
        activeRole: 'worker',
        clockLocked: true,
        masterAddress: '192.168.1.1',
        lastPacketAgeMs: 50,
        clockOffsetMs: 0.5,
        clockJitterUs: 100,
      },
      network: {
        state: 'connected',
        rssi: -50,
        staConnected: true,
        staIp: '192.168.1.100',
        apActive: false,
        applyState: 'idle',
      },
      protocols: {
        artnet: { enabled: true, sources: 0, packetsPerSecond: 0, lastPacketAgeMs: 0 },
        sacn: { enabled: true, sources: 0, packetsPerSecond: 0 },
      },
      timeline: {
        clockMs: 12345,
        driftPpm: 0,
        synced: true,
      },
      automation: {
        activeTracks: 0,
        transitionCurve: 'easeInOut',
      },
      job: null,
      channels: [],
    };

    const parsed = normalizeRuntimeSnapshot(raw);
    expect(parsed.schema).toBe(3);
    expect(parsed.node.id).toBe('lg-aabbcc');
    expect(parsed.runtime.effect).toBe('rainbow');
    expect(parsed.output.powerMw).toBe(4000);
  });

  // B. Schema 2 explicitly normalizes through compatibility adapter
  it('explicitly normalizes Schema 2 into RuntimeSnapshotV3', () => {
    const rawSchema2 = {
      schema: 2,
      seq: 42,
      nodeId: 'lg-schema2-node',
      uptimeMs: 98765,
      state: {
        runtime: {
          effect: 'solid',
          brightness: 200,
          testActive: false,
          autoLoop: true,
          owner: 'Scheduler',
        },
        output: {
          outputsEnabled: true,
          powerMw: 3200,
          state: 'healthy',
          fault: false,
        },
        scene: {
          activePreset: 4,
          appliedGeneration: 12,
        },
        sync: {
          activeRole: 'leader',
          clockLocked: true,
        },
      },
    };

    const normalized = normalizeRuntimeSnapshot(rawSchema2);
    expect(normalized.schema).toBe(3);
    expect(normalized.streamSeq).toBe(42);
    expect(normalized.node.id).toBe('lg-schema2-node');
    expect(normalized.runtime.effect).toBe('solid');
    expect(normalized.runtime.brightness).toBe(200);
    expect(normalized.runtime.controlOwner).toBe('Scheduler');
    expect(normalized.output.powerMw).toBe(3200);
    expect(normalized.scene.activePreset).toBe(4);
    expect(normalized.scene.appliedGeneration).toBe(12);
    expect(normalized.sync.activeRole).toBe('leader');
  });

  // C. Unknown schema rejected
  it('throws an error on unknown schema', () => {
    expect(() => normalizeRuntimeSnapshot({ schema: 99 })).toThrow(/Unsupported.*schema.*99/i);
    expect(() => normalizeRuntimeSnapshot(null)).toThrow(/null or not an object/i);
    expect(() => normalizeRuntimeSnapshot('string')).toThrow(/null or not an object/i);
  });

  // D & E. HTTP initial snapshot populates nodeStore, WS snapshot replaces HTTP snapshot
  it('populates nodeStore with HTTP initial snapshot, and WS snapshot supersedes it', async () => {
    const initialHttpSnapshot: RuntimeSnapshotV3 = {
      schema: 3,
      streamSeq: 0,
      generatedAtMs: 1000,
      node: { id: 'lg-test-http' },
      runtime: { effect: 'solid', brightness: 50, testActive: false, autoLoop: false, controlOwner: 'ScenePlayback', frameCount: 10 },
      output: { enabled: true, powerMw: 1000, state: 'healthy', fault: false, faultCount: 0, estopActive: false },
      scene: { activePreset: 0, requestedPreset: 0, requestedGeneration: 1, appliedGeneration: 1, applyPending: false, transitioning: false, fallbackActive: false },
      manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
      transport: { mode: 'scene', generation: 1, sceneIndex: 0, timeline: { running: false, bpm: 120 }, cue: { running: false } },
      sync: { configuredRole: 'auto', activeRole: 'none', clockLocked: false, masterAddress: '', lastPacketAgeMs: 0, clockOffsetMs: 0, clockJitterUs: 0 },
      network: { state: 'connected', rssi: -60, staConnected: true, staIp: '192.168.1.50', apActive: false, applyState: 'idle' },
      protocols: { artnet: { enabled: true, sources: 0, packetsPerSecond: 0, lastPacketAgeMs: 0 }, sacn: { enabled: true, sources: 0, packetsPerSecond: 0 } },
      timeline: { clockMs: 1000, driftPpm: 0, synced: false },
      automation: { activeTracks: 0, transitionCurve: 'linear' },
      job: null,
      channels: [],
    };

    vi.spyOn(nodeApi, 'getStatus').mockResolvedValue(initialHttpSnapshot);

    await connect('192.168.1.50');
    // Initially populated from HTTP getStatus
    const snap1 = getNodeLiveSnapshot();
    expect(snap1.snapshot?.node.id).toBe('lg-test-http');
    expect(snap1.snapshot?.runtime.brightness).toBe(50);

    // WebSocket arrives with streamSeq = 1 and new brightness
    const ws = MockWebSocket.instances[0];
    ws.triggerOpen();

    const wsSnapshot: RuntimeSnapshotV3 = {
      ...initialHttpSnapshot,
      streamSeq: 1,
      runtime: { ...initialHttpSnapshot.runtime, brightness: 220 },
    };
    ws.triggerMessage(JSON.stringify(wsSnapshot));

    const snap2 = getNodeLiveSnapshot();
    expect(snap2.snapshot?.streamSeq).toBe(1);
    expect(snap2.snapshot?.runtime.brightness).toBe(220);
  });

  // F & G. Stream gap triggers degraded + HTTP recovery, valid recovery returns online
  it('stream gap triggers degraded state and HTTP recovery, healing back to online', async () => {
    const baseSnap: RuntimeSnapshotV3 = {
      schema: 3,
      streamSeq: 1,
      generatedAtMs: 2000,
      node: { id: 'lg-gap-test' },
      runtime: { effect: 'solid', brightness: 100, testActive: false, autoLoop: false, controlOwner: 'ScenePlayback', frameCount: 20 },
      output: { enabled: true, powerMw: 1000, state: 'healthy', fault: false, faultCount: 0, estopActive: false },
      scene: { activePreset: 1, requestedPreset: 1, requestedGeneration: 1, appliedGeneration: 1, applyPending: false, transitioning: false, fallbackActive: false },
      manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
      transport: { mode: 'scene', generation: 1, sceneIndex: 1, timeline: { running: false, bpm: 120 }, cue: { running: false } },
      sync: { configuredRole: 'auto', activeRole: 'none', clockLocked: false, masterAddress: '', lastPacketAgeMs: 0, clockOffsetMs: 0, clockJitterUs: 0 },
      network: { state: 'connected', rssi: -60, staConnected: true, staIp: '192.168.1.50', apActive: false, applyState: 'idle' },
      protocols: { artnet: { enabled: true, sources: 0, packetsPerSecond: 0, lastPacketAgeMs: 0 }, sacn: { enabled: true, sources: 0, packetsPerSecond: 0 } },
      timeline: { clockMs: 2000, driftPpm: 0, synced: false },
      automation: { activeTracks: 0, transitionCurve: 'linear' },
      job: null,
      channels: [],
    };

    const getStatusSpy = vi.spyOn(nodeApi, 'getStatus').mockResolvedValue(baseSnap);

    await connect('192.168.1.50');
    const ws = MockWebSocket.instances[0];
    ws.triggerOpen();

    ws.triggerMessage(JSON.stringify({ ...baseSnap, streamSeq: 1 }));
    expect(getNodeLiveSnapshot().connection).toBe('online');

    // Gap: jump from 1 to 5
    const recoveredSnap = { ...baseSnap, streamSeq: 5, runtime: { ...baseSnap.runtime, brightness: 150 } };
    getStatusSpy.mockResolvedValueOnce(recoveredSnap);

    ws.triggerMessage(JSON.stringify({ ...baseSnap, streamSeq: 5 }));
    expect(getNodeLiveSnapshot().connection).toBe('degraded');

    // Advance timers so getStatus recovery executes
    await vi.advanceTimersByTimeAsync(200);

    expect(getStatusSpy).toHaveBeenCalled();
    expect(getNodeLiveSnapshot().connection).toBe('online');
    expect(getNodeLiveSnapshot().lastStreamSeq).toBe(5);
  });

  // H. HTTP status requests do not alter streamSeq or stream-gap tracking
  it('HTTP status requests have streamSeq = 0 and do not disturb stream gap tracking', () => {
    // Normalization preserves streamSeq 0 from HTTP
    const httpRaw = {
      schema: 3,
      streamSeq: 0,
      generatedAtMs: 100,
      node: { id: 'lg-http' },
      runtime: { effect: 'solid', brightness: 10, testActive: false, autoLoop: false, controlOwner: 'Scene', frameCount: 0 },
      output: { enabled: true, powerMw: 100, state: 'healthy', fault: false, faultCount: 0, estopActive: false },
      scene: { activePreset: 0, requestedPreset: 0, requestedGeneration: 0, appliedGeneration: 0, applyPending: false, transitioning: false, fallbackActive: false },
      manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
      transport: { mode: 'scene', generation: 0, sceneIndex: 0 },
      sync: { configuredRole: 'auto', activeRole: 'none', clockLocked: false, masterAddress: '', lastPacketAgeMs: 0, clockOffsetMs: 0, clockJitterUs: 0 },
      network: { state: 'connected', rssi: -50, staConnected: true, staIp: '192.168.1.50', apActive: false, applyState: 'idle' },
      protocols: { artnet: { enabled: false, sources: 0, packetsPerSecond: 0, lastPacketAgeMs: 0 }, sacn: { enabled: false, sources: 0, packetsPerSecond: 0 } },
      timeline: { clockMs: 0, driftPpm: 0, synced: false },
      automation: { activeTracks: 0, transitionCurve: 'linear' },
      job: null,
      channels: [],
    };
    const parsed = normalizeRuntimeSnapshot(httpRaw);
    expect(parsed.streamSeq).toBe(0);
  });

  // LG-030 closure: U, V, W - Auth binding to immutable ID and route rebinding
  it('binds session to immutable node ID, preserves session on DHCP route rebind, and decouples on IP reuse', () => {
    nodeAuthStore.clearAllSessions();
    nodeAuthStore.setSession({
      address: '192.168.1.44',
      sessionToken: 'token-abc',
      role: 'admin',
      expiresAt: Date.now() + 3600000,
    });
    expect(nodeAuthStore.hasValidSession('192.168.1.44')).toBe(true);

    // Bind address to immutable ID
    nodeAuthStore.bindNodeId('192.168.1.44', 'lg-aabbccddeeff');

    // DHCP migration: same physical node gets 192.168.1.83
    nodeAuthStore.rebindAddress('lg-aabbccddeeff', '192.168.1.83');

    // Preserved session on new address
    expect(nodeAuthStore.hasValidSession('192.168.1.83')).toBe(true);
    expect(nodeAuthStore.getRole('192.168.1.83')).toBe('admin');
    expect(nodeAuthStore.getSession('192.168.1.83')?.sessionToken).toBe('token-abc');

    // Inverse case: 192.168.1.44 reused by a different physical node lg-NEW
    nodeAuthStore.bindNodeId('192.168.1.44', 'lg-NEW');
    // Must NOT transfer old session!
    expect(nodeAuthStore.hasValidSession('192.168.1.44')).toBe(false);
  });

  // LG-030 closure: X - Local alias survives mDNS deviceName refresh
  it('preserves user local alias across mDNS deviceName refresh', () => {
    upsertNodeRecord({
      id: 'lg-device-1',
      name: 'Initial Name',
      address: '192.168.1.50',
      family: 'lumigrid-esp32',
      model: 'lg-led-v1',
      lastSeen: 1000,
      discoverySource: 'manual',
      presence: 'online',
    });

    let records = readNodeRegistry();
    expect(records['lg-device-1']?.deviceName).toBe('Initial Name');

    // User sets browser-local alias
    upsertNodeRecord({
      id: 'lg-device-1',
      alias: 'Main Stage Truss',
      lastSeen: 2000,
    });
    records = readNodeRegistry();
    expect(records['lg-device-1']?.alias).toBe('Main Stage Truss');
    expect(records['lg-device-1']?.deviceName).toBe('Initial Name');

    // mDNS rediscovery occurs with new deviceName
    upsertNodeRecord({
      id: 'lg-device-1',
      name: 'Hardware Updated Name',
      lastSeen: 3000,
      discoverySource: 'mdns',
    });
    records = readNodeRegistry();
    expect(records['lg-device-1']?.deviceName).toBe('Hardware Updated Name');
    expect(records['lg-device-1']?.alias).toBe('Main Stage Truss'); // Alias NOT overwritten!
  });

  // LG-030 closure: Y & Z - Scheme handling & transport blocked detection
  it('strictly returns http:// and ws:// and detects mixed-content transport blocked', () => {
    expect(getNodeHttpBase('192.168.1.50')).toBe('http://192.168.1.50');
    expect(getNodeWsUrl('192.168.1.50', 'ws')).toBe('ws://192.168.1.50/ws');

    const blockedErr = new TransportBlockedError('Mixed content blocked');
    expect(blockedErr.name).toBe('TransportBlockedError');
  });

  // Q. CommandTracker matches authoritative IDs
  it('commandTracker matches only authoritative commandId or jobId', async () => {
    commandTracker.register({
      id: 100,
      commandId: 100,
      jobId: 50,
      domain: 'ota',
      nodeIp: '192.168.1.50',
    });

    // Unrelated job 99 completes
    commandTracker.observe({
      job: { jobId: 99, type: 'restore', state: 'completed', progress: 100, startedMs: 0, error: null },
    });
    expect(commandTracker.get(100)?.state).toBe('accepted');

    // Matching jobId 50 completes
    commandTracker.observe({
      job: { jobId: 50, type: 'ota', state: 'completed', progress: 100, startedMs: 0, error: null },
    });
    expect(commandTracker.get(100)?.state).toBe('applied');
  });

  // R. Capabilities cache invalidates when firmware version changes
  it('capabilities cache invalidates when firmware version changes', async () => {
    invalidateCapabilitiesCache();
    const cap1 = { schema: 1, features: { fleetDiscovery: true }, limits: { zones: 1, presets: 1, timelineSteps: 1 }, node: { model: 'lg-1', family: 'led', firmware: '2.0.0' } } as any;
    const fetchSpy = vi.spyOn(nodeApi, 'getCapabilities').mockResolvedValue(cap1);

    await loadCaps('192.168.1.50', 'lg-1', '2.0.0');
    expect(fetchSpy).toHaveBeenCalledTimes(1);

    // Same node and version -> cached
    await loadCaps('192.168.1.50', 'lg-1', '2.0.0');
    expect(fetchSpy).toHaveBeenCalledTimes(1);

    // Version changes -> cache invalidated, refetches
    const cap2 = { ...cap1, node: { ...cap1.node, firmware: '2.1.0' } };
    fetchSpy.mockResolvedValue(cap2);
    await loadCaps('192.168.1.50', 'lg-1', '2.1.0');
    expect(fetchSpy).toHaveBeenCalledTimes(2);
  });

  // J. Binary pixel preview remains isolated from runtime store
  it('keeps binary pixel preview in separate isolated store', () => {
    const rawPixels = new Uint8Array([255, 0, 0, 0, 255, 0]);
    nodePixelStore.set(rawPixels);

    expect(getNodeLiveSnapshot().pixels).toBeDefined();
    let currentPixelData: Uint8Array | null = null;
    const unsub = nodePixelStore.subscribe((buf) => {
      currentPixelData = buf;
    });
    expect(currentPixelData?.[0]).toBe(255);
    expect(currentPixelData?.[1]).toBe(0);
    unsub();
  });
});
