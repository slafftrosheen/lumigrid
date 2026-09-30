/**
 * Shared live node connection/state for dashboard views (LG-031).
 *
 * The node remains the source of truth. nodeStore is the authoritative
 * live-state manager for the focused node, owning connection lifecycle,
 * RuntimeSnapshot v3 propagation, WebSocket connection, and pixel streams.
 */

import { writable, derived } from 'svelte/store';
import {
  getSelectedNodeIp,
  setSelectedNodeIp,
  updateNodeLastSeen,
  recordDiscoveredNode,
} from '../auth/storage';
import { nodeAuthStore } from '../auth/nodeAuth';
import { createWsTicket } from '../api/auth';
import { getHealth, getStatus, type HealthSnapshot } from '../api/node';
import { getNodeWsUrl, isTransportBlockedError } from '../api/transport';
import { commandTracker } from './commandTracker';
import type { RuntimeSnapshotV3 } from './runtimeSnapshot';
import { normalizeRuntimeSnapshot } from './compat/runtimeSnapshotV2';

export { commandTracker };
export type { RuntimeSnapshotV3 };

export const LANES = 8;
export const PIXELS_PER_LANE = 144;
export const BUF_SIZE = LANES * PIXELS_PER_LANE * 3;

export type NodeConnection =
  | 'idle'
  | 'connecting'
  | 'online'
  | 'degraded'
  | 'offline'
  | 'auth-required'
  | 'transport-blocked'
  // Backward compatibility aliases:
  | 'connected'
  | 'disconnected'
  | 'error';

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
  activePreset?: number;
  appliedGeneration?: number;
  requestedGeneration?: number;
  sceneApplyPending?: boolean;
  transitioning?: boolean;
  controlOwner?: string;
  powerMw?: number;
  fault?: boolean;
  faultCount?: number;
  seq?: number;
  nodeId?: string;
  uptimeMs?: number;
  protocol?: {
    artnet?: { enabled: boolean; sources: number; packetsPerSecond: number; lastPacketAgeMs: number };
    sacn?: { enabled: boolean; sources: number; packetsPerSecond: number };
    owner?: string;
  };
  job?: {
    jobId: number;
    type: string;
    state: string;
    progress: number;
    startedMs: number;
    error: string | null;
  } | null;
  timeline?: {
    clockMs: number;
    driftPpm: number;
    synced: boolean;
  };
  automation?: {
    activeTracks: number;
    transitionCurve: string;
  };
  cue?: {
    running: boolean;
    state?: string;
    listIndex?: number;
    cueIndex?: number;
    nextCueIndex?: number;
    mode?: string;
    elapsedMs?: number;
    remainingMs?: number;
    manualFade?: number;
  } | null;
  channels?: Array<{
    index: number;
    port: string;
    configured: boolean;
    enabled: boolean;
    owner: string;
    fault: boolean;
    brightness?: number;
    pixels?: number;
    level?: number;
  }>;
  manual?: {
    active: boolean;
    channelMask: number;
    leaseRemainingMs: number;
  };
}

export interface NodeLiveState {
  nodeId: string | null;
  route: string | null;

  connection: NodeConnection;

  snapshot: RuntimeSnapshotV3 | null;

  health: HealthSnapshot | null;
  healthError: string;
  healthUpdatedAt: number;

  lastSnapshotAt: number;
  lastStreamSeq: number;

  error: string | null;

  // Backward compatibility fields
  nodeIp: string;
  ip: string;
  dashboardUnlocked: boolean;
  dashboardRole: NodeRole;
  authenticated: boolean;
  role: NodeRole;
  lastSeen: number;
  lastMessageAt: number;
  status: NodeLiveStatus | null;
  pixels: Uint8Array;
  frameCount: number;
}

function projectLegacyStatus(snap: RuntimeSnapshotV3 | null): NodeLiveStatus | null {
  if (!snap) return null;
  return {
    effect: snap.runtime.effect,
    brightness: snap.runtime.brightness,
    outputsEnabled: snap.output.enabled,
    testActive: snap.runtime.testActive,
    autoLoop: snap.runtime.autoLoop,
    activePreset: snap.scene.activePreset,
    appliedGeneration: snap.scene.appliedGeneration,
    requestedGeneration: snap.scene.requestedGeneration,
    sceneApplyPending: snap.scene.applyPending,
    transitioning: snap.scene.transitioning,
    controlOwner: snap.runtime.controlOwner,
    powerMw: snap.output.powerMw,
    fault: snap.output.fault,
    faultCount: snap.output.faultCount,
    seq: snap.streamSeq,
    nodeId: snap.node.id,
    uptimeMs: snap.generatedAtMs,
    protocol: {
      artnet: snap.protocols.artnet ? {
        enabled: snap.protocols.artnet.enabled,
        sources: snap.protocols.artnet.sources,
        packetsPerSecond: snap.protocols.artnet.packetsPerSecond,
        lastPacketAgeMs: snap.protocols.artnet.lastPacketAgeMs || 0,
      } : undefined,
      sacn: snap.protocols.sacn ? {
        enabled: snap.protocols.sacn.enabled,
        sources: snap.protocols.sacn.sources,
        packetsPerSecond: snap.protocols.sacn.packetsPerSecond,
      } : undefined,
      owner: snap.runtime.controlOwner,
    },
    job: snap.job ? { ...snap.job } : null,
    timeline: {
      clockMs: snap.timeline.clockMs,
      driftPpm: snap.timeline.driftPpm,
      synced: snap.timeline.synced,
    },
    automation: {
      activeTracks: snap.automation.activeTracks,
      transitionCurve: snap.automation.transitionCurve,
    },
    cue: snap.transport.cue ? {
      running: snap.transport.cue.running,
      state: snap.transport.cue.state,
      listIndex: snap.transport.cue.listIndex,
      cueIndex: snap.transport.cue.cueIndex,
      nextCueIndex: snap.transport.cue.nextCueIndex,
      manualFade: snap.transport.cue.manualFade,
    } : null,
    channels: snap.channels ? snap.channels.map(c => ({ ...c })) : [],
    manual: {
      active: snap.manual.active,
      channelMask: snap.manual.channelMask,
      leaseRemainingMs: snap.manual.leaseRemainingMs,
    },
  };
}

const initial: NodeLiveState = {
  nodeId: null,
  route: null,
  connection: 'idle',
  snapshot: null,
  health: null,
  healthError: '',
  healthUpdatedAt: 0,
  lastSnapshotAt: 0,
  lastStreamSeq: 0,
  error: null,

  nodeIp: '',
  ip: '',
  dashboardUnlocked: false,
  dashboardRole: 'unknown',
  authenticated: false,
  role: 'unknown',
  lastSeen: 0,
  lastMessageAt: 0,
  status: {
    effect: '',
    brightness: 0,
    outputsEnabled: true,
    testActive: false,
    autoLoop: false,
    activePreset: 0,
    appliedGeneration: 0,
    requestedGeneration: 0,
    sceneApplyPending: false,
    transitioning: false,
    controlOwner: 'ScenePlayback',
    powerMw: 0,
    fault: false,
    faultCount: 0,
    seq: 0,
    nodeId: '',
    uptimeMs: 0,
    protocol: {},
    channels: [],
    manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
  },
  pixels: new Uint8Array(BUF_SIZE),
  frameCount: 0,
};

// Main authoritative node live state store
const state = writable<NodeLiveState>(initial);
let latestState = initial;
state.subscribe((value) => {
  latestState = value;
});

// Derived store selectors for high-rate efficiency
export const sceneRuntimeStore = derived(state, ($s) => $s.snapshot?.scene ?? null);
export const outputRuntimeStore = derived(state, ($s) => $s.snapshot?.output ?? null);
export const networkRuntimeStore = derived(state, ($s) => $s.snapshot?.network ?? null);
export const syncRuntimeStore = derived(state, ($s) => $s.snapshot?.sync ?? null);
export const transportRuntimeStore = derived(state, ($s) => $s.snapshot?.transport ?? null);
export const channelRuntimeStore = derived(state, ($s) => $s.snapshot?.channels ?? []);
export const jobRuntimeStore = derived(state, ($s) => $s.snapshot?.job ?? null);

// Dedicated isolated pixel stores
export const nodePixelStore = writable<Uint8Array>(new Uint8Array(BUF_SIZE));
export const nodeFrameCount = writable<number>(0);

let socket: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let healthPollTimer: ReturnType<typeof setInterval> | null = null;
let reconnectAttempt = 0;
let manualClose = false;
let lastSeq = 0;

function getStoredRole(ip = latestState.route || latestState.nodeIp || getSelectedNodeIp()): NodeRole {
  if (!ip) return 'unknown';
  const session = nodeAuthStore.getSession(ip);
  if (session) return session.role;
  return 'unknown';
}

function isStoredAuthed(ip = latestState.route || latestState.nodeIp || getSelectedNodeIp()): boolean {
  if (!ip) return false;
  return nodeAuthStore.hasValidSession(ip);
}

function publish(patch: Partial<NodeLiveState>) {
  state.update((current) => {
    const next = { ...current, ...patch };
    const ipVal = patch.route ?? patch.nodeIp ?? patch.ip;
    if (ipVal !== undefined) {
      next.route = ipVal;
      next.nodeIp = ipVal;
      next.ip = ipVal;
    }
    const msgVal = patch.lastMessageAt ?? patch.lastSeen ?? patch.lastSnapshotAt;
    if (msgVal !== undefined) {
      next.lastMessageAt = msgVal;
      next.lastSeen = msgVal;
      next.lastSnapshotAt = msgVal;
    }
    if (patch.snapshot !== undefined) {
      next.status = projectLegacyStatus(patch.snapshot);
    }
    return next;
  });
}

export function getWsUrl(ip: string): string {
  return getNodeWsUrl(ip);
}

let lastFleetUpdate = 0;
function recordFleetPresence(ip: string) {
  if (!ip) return;
  const now = Date.now();
  if (now - lastFleetUpdate > 5000) {
    lastFleetUpdate = now;
    try {
      updateNodeLastSeen(ip);
    } catch {}
  }
}

function scheduleReconnect() {
  if (manualClose || reconnectTimer) return;
  const targetIp = latestState.route || latestState.nodeIp || getSelectedNodeIp();
  if (!targetIp || !nodeAuthStore.hasValidSession(targetIp)) return;

  const delay = Math.min(10000, 500 * Math.pow(2, reconnectAttempt));
  reconnectAttempt = Math.min(reconnectAttempt + 1, 5);
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect(targetIp);
  }, delay);
}

export async function refreshHealth(nodeIp = latestState.route || latestState.nodeIp || getSelectedNodeIp()): Promise<HealthSnapshot | null> {
  const ip = nodeIp.trim();
  if (!ip) return null;
  try {
    const health = await getHealth(ip);
    publish({ health, healthError: '', healthUpdatedAt: Date.now() });
    recordFleetPresence(ip);
    return health;
  } catch (err) {
    if (isTransportBlockedError(err)) {
      publish({
        connection: 'transport-blocked',
        error: 'Direct local-node access is blocked by this browser context. Open the dashboard directly from a LumiGrid node.',
      });
      return null;
    }
    const healthError = err instanceof Error ? err.message : String(err);
    publish({ healthError, healthUpdatedAt: Date.now() });
    return null;
  }
}

/**
 * Perform initial state recovery or HTTP gap recovery via GET /api/v1/status.
 * Requirement 15: populates nodeStore deterministically before WS opens.
 */
export async function recoverStateViaHttp(ip: string): Promise<RuntimeSnapshotV3 | null> {
  try {
    const snapshot = await getStatus(ip);
    if (!snapshot) return null;

    if (snapshot.node && snapshot.node.id) {
      nodeAuthStore.bindNodeId(ip, snapshot.node.id);
      try {
        recordDiscoveredNode({ id: snapshot.node.id, address: ip, name: snapshot.node.id }, 'sync');
      } catch {}
    }

    commandTracker.observe(snapshot as any);

    publish({
      nodeId: snapshot.node.id,
      snapshot,
      lastSnapshotAt: Date.now(),
      error: null,
      connection: latestState.connection === 'degraded' || latestState.connection === 'connecting'
        ? (socket && (socket.readyState === 1 || socket.readyState === (WebSocket as any).OPEN) ? 'online' : latestState.connection)
        : latestState.connection,
    });
    return snapshot;
  } catch (err) {
    if (isTransportBlockedError(err)) {
      publish({
        connection: 'transport-blocked',
        error: 'Direct local-node access is blocked by this browser context. Open the dashboard directly from a LumiGrid node.',
      });
    }
    return null;
  }
}

export async function connect(nodeIp = getSelectedNodeIp()): Promise<void> {
  if (typeof WebSocket === 'undefined') return;

  const ip = nodeIp.trim();
  if (!ip) {
    setSelectedNodeIp('');
    if (socket) {
      const ws = socket;
      socket = null;
      ws.close();
    }
    publish({ route: '', nodeIp: '', ip: '', connection: 'idle', error: null, snapshot: null });
    return;
  }
  setSelectedNodeIp(ip);

  // If already connected/connecting to the exact same IP, return early
  if (
    socket &&
    latestState.route === ip &&
    (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)
  ) {
    return;
  }

  // If switching to a different node, close existing socket and clear reconnect timer
  if (socket && latestState.route !== ip) {
    const ws = socket;
    socket = null;
    ws.close();
  }

  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  if (healthPollTimer) {
    clearInterval(healthPollTimer);
    healthPollTimer = null;
  }

  manualClose = false;
  const currentRole = getStoredRole(ip);
  const currentUnlocked = isStoredAuthed(ip);

  if (!currentUnlocked) {
    publish({
      route: ip,
      nodeIp: ip,
      ip,
      connection: 'auth-required',
      dashboardRole: currentRole,
      dashboardUnlocked: false,
      role: currentRole,
      authenticated: false,
      error: 'Authentication required',
    });
    return;
  }

  publish({
    route: ip,
    nodeIp: ip,
    ip,
    connection: 'connecting',
    dashboardRole: currentRole,
    dashboardUnlocked: currentUnlocked,
    role: currentRole,
    authenticated: currentUnlocked,
    error: null,
  });

  // Requirement 15: Initial HTTP status fetch before connecting WebSocket
  recoverStateViaHttp(ip).catch(() => {});

  try {
    const { ticket } = await createWsTicket(ip);
    if (manualClose || latestState.route !== ip) return;

    const wsUrl = getNodeWsUrl(ip);
    const ws = new WebSocket(wsUrl, [`lumigrid-ticket.${ticket}`]);
    socket = ws;
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
      if (socket !== ws) return;
      reconnectAttempt = 0;
      const openRole = getStoredRole(ip);
      const openUnlocked = isStoredAuthed(ip);
      publish({
        connection: 'online',
        dashboardRole: openRole,
        dashboardUnlocked: openUnlocked,
        role: openRole,
        authenticated: openUnlocked,
        error: null,
      });

      // Proactively pull health snapshot on connect
      refreshHealth(ip).catch(() => {});

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
      publish({ connection: 'offline' });
      scheduleReconnect();
    };

    ws.onerror = () => {
      if (socket !== ws) return;
      publish({ connection: 'offline', error: 'WebSocket connection error' });
    };

    ws.onmessage = (event) => {
      if (socket !== ws) return;
      const now = Date.now();

      // Binary Pixel Frame: isolated high-frequency rendering path (Requirement 32)
      if (event.data instanceof ArrayBuffer) {
        const arr = new Uint8Array(event.data);
        if (arr.length > 1 && (arr[0] === 1 || arr[0] === 76)) {
          const raw = arr.subarray(1);
          const len = Math.min(raw.length, BUF_SIZE);
          const pixels = new Uint8Array(BUF_SIZE);
          pixels.set(raw.subarray(0, len));

          nodePixelStore.set(pixels);
          nodeFrameCount.update((n) => n + 1);

          publish({
            pixels,
            frameCount: latestState.frameCount + 1,
            lastSeen: now,
            lastMessageAt: now,
          });
        }
        return;
      }

      // JSON RuntimeSnapshot path
      if (typeof event.data === 'string') {
        try {
          const raw = JSON.parse(event.data);
          let snapshot = normalizeRuntimeSnapshot(raw);
          if (!snapshot) return;

          if (raw.schema === undefined && latestState.snapshot) {
            snapshot = {
              ...latestState.snapshot,
              streamSeq: snapshot.streamSeq || latestState.snapshot.streamSeq,
              runtime: {
                ...latestState.snapshot.runtime,
                ...snapshot.runtime,
                effect: raw.effect !== undefined ? String(raw.effect) : latestState.snapshot.runtime.effect,
                brightness: raw.brightness !== undefined ? Number(raw.brightness) : latestState.snapshot.runtime.brightness,
                testActive: raw.testActive !== undefined ? Boolean(raw.testActive) : latestState.snapshot.runtime.testActive,
                autoLoop: raw.autoLoop !== undefined ? Boolean(raw.autoLoop) : latestState.snapshot.runtime.autoLoop,
              },
              output: {
                ...latestState.snapshot.output,
                enabled: raw.outputsEnabled !== undefined ? Boolean(raw.outputsEnabled) : latestState.snapshot.output.enabled,
              },
            };
          }

          // Requirement 7 & 16: Stream sequence gap tracking
          const streamSeq = snapshot.streamSeq;
          let connStatus: NodeConnection = latestState.connection;
          if (streamSeq > 0) {
            if (lastSeq > 0 && streamSeq > lastSeq + 1) {
              connStatus = 'degraded';
              // Gap detected: recover state via HTTP
              recoverStateViaHttp(latestState.route || ip).catch(() => {});
            } else if (connStatus === 'degraded') {
              connStatus = 'online';
            }
            lastSeq = streamSeq;
          }

          // Requirement 0.2: Bind authenticated node ID
          if (snapshot.node && snapshot.node.id) {
            nodeAuthStore.bindNodeId(ip, snapshot.node.id);
            try {
              recordDiscoveredNode({ id: snapshot.node.id, address: ip, name: snapshot.node.id }, 'sync');
            } catch {}
          }

          // Inform CommandTracker
          commandTracker.observe(snapshot as any);

          publish({
            nodeId: snapshot.node.id,
            connection: connStatus,
            snapshot,
            lastSnapshotAt: now,
            lastStreamSeq: streamSeq,
            lastSeen: now,
            lastMessageAt: now,
            error: null,
          });

          recordFleetPresence(latestState.route || ip);
        } catch {
          // Defensive: ignore malformed frames
        }
      }
    };
  } catch (err: unknown) {
    if (manualClose || latestState.route !== ip) return;

    if (isTransportBlockedError(err)) {
      publish({
        connection: 'transport-blocked',
        error: 'Direct local-node access is blocked by this browser context. Open the dashboard directly from a LumiGrid node.',
      });
      return;
    }

    const msg = err instanceof Error ? err.message : String(err);
    const isAuthErr =
      msg.includes('401') ||
      msg.toLowerCase().includes('auth') ||
      msg.toLowerCase().includes('session');

    publish({
      connection: isAuthErr ? 'auth-required' : 'offline',
      dashboardUnlocked: isAuthErr ? false : latestState.dashboardUnlocked,
      authenticated: isAuthErr ? false : latestState.authenticated,
      error: isAuthErr ? 'Authentication required' : msg,
    });

    if (!isAuthErr) {
      scheduleReconnect();
    }
  }
}

export function disconnect(): void {
  manualClose = true;
  lastSeq = 0;
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
  publish({ connection: latestState.route || latestState.nodeIp ? 'offline' : 'idle' });
}

export async function reconnect(ip?: string): Promise<void> {
  disconnect();
  manualClose = false;
  const target = ip ?? latestState.route ?? latestState.nodeIp ?? getSelectedNodeIp();
  await connect(target);
}

export const nodeStore = state;

export function selectNode(ip: string): void {
  const target = (ip || '').trim();
  setSelectedNodeIp(target);
  if (!target) {
    disconnect();
    publish({ route: '', nodeIp: '', ip: '', connection: 'idle', snapshot: null });
  } else {
    connect(target);
  }
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

export function _resetForTesting(): void {
  disconnect();
  manualClose = false;
  reconnectAttempt = 0;
  lastSeq = 0;
  commandTracker.clear();
  state.set({ ...initial, pixels: new Uint8Array(BUF_SIZE) });
  nodePixelStore.set(new Uint8Array(BUF_SIZE));
  nodeFrameCount.set(0);
}

if (typeof window !== 'undefined') {
  nodeAuthStore.subscribe(() => {
    const currentIp = latestState.route || latestState.nodeIp;
    if (!currentIp) return;
    const hasSession = nodeAuthStore.hasValidSession(currentIp);
    const role = getStoredRole(currentIp);
    if (
      hasSession &&
      (latestState.connection === 'offline' ||
        latestState.connection === 'idle' ||
        latestState.connection === 'auth-required' ||
        latestState.error === 'Authentication required')
    ) {
      connect(currentIp);
    } else if (
      !hasSession &&
      (latestState.connection === 'online' || latestState.connection === 'connecting')
    ) {
      disconnect();
      publish({
        dashboardRole: 'unknown',
        dashboardUnlocked: false,
        role: 'unknown',
        authenticated: false,
        error: 'Authentication required',
        connection: 'auth-required',
      });
    } else if (hasSession) {
      publish({
        dashboardRole: role,
        dashboardUnlocked: true,
        role,
        authenticated: true,
      });
    }
  });
}
