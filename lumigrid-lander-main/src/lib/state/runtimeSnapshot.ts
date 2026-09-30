/**
 * Canonical RuntimeSnapshot v3 (LG-031).
 * Single authoritative live-state contract shared identically across
 * GET /api/v1/status and WebSocket broadcasts.
 */

export interface RuntimeNodeState {
  id: string;
}

export interface RuntimeState {
  effect: string;
  brightness: number;
  testActive: boolean;
  autoLoop: boolean;
  controlOwner: string;
  frameCount: number;
}

export interface OutputState {
  enabled: boolean;
  powerMw: number;
  state: 'healthy' | 'armed' | 'fault' | 'estop' | string;
  fault: boolean;
  faultCount: number;
  estopActive: boolean;
}

export interface SceneRuntimeState {
  activePreset: number;
  requestedPreset: number;
  requestedGeneration: number;
  appliedGeneration: number;
  applyPending: boolean;
  transitioning: boolean;
  fallbackActive: boolean;
}

export interface ManualRuntimeState {
  active: boolean;
  channelMask: number;
  leaseRemainingMs: number;
}

export interface TransportTimelineRuntime {
  running: boolean;
  bpm: number;
}

export interface TransportCueRuntime {
  running: boolean;
  state?: 'stopped' | 'holding' | 'fading' | 'paused' | string;
  listIndex?: number;
  cueIndex?: number;
  nextCueIndex?: number;
  manualFade?: number;
  elapsedMs?: number;
  remainingMs?: number;
}

export interface TransportRuntimeState {
  mode: 'scene' | 'timeline' | 'cue' | string;
  generation: number;
  sceneIndex: number;
  timeline: TransportTimelineRuntime;
  cue: TransportCueRuntime;
}

export interface SyncRuntimeState {
  configuredRole: 'auto' | 'master' | 'worker' | 'leader' | 'follower' | string;
  activeRole: 'auto' | 'master' | 'worker' | 'leader' | 'follower' | string;
  clockLocked: boolean;
  masterAddress?: string;
  lastPacketAgeMs?: number;
  clockOffsetMs?: number;
  clockJitterUs?: number;
}

export interface NetworkRuntimeState {
  state: 'connected' | 'connecting' | 'disconnected' | 'ap_only' | string;
  rssi: number;
  staConnected: boolean;
  staIp: string;
  apActive: boolean;
  applyState: 'idle' | 'applying' | 'failed' | string;
}

export interface ProtocolStreamState {
  enabled: boolean;
  sources: number;
  packetsPerSecond: number;
  lastPacketAgeMs?: number;
}

export interface ProtocolRuntimeState {
  artnet: ProtocolStreamState;
  sacn: ProtocolStreamState;
}

export interface TimelineRuntimeState {
  clockMs: number;
  driftPpm: number;
  synced: boolean;
}

export interface AutomationRuntimeState {
  activeTracks: number;
  transitionCurve: string;
}

export interface OperationJob {
  jobId: number;
  type: string;
  state: 'idle' | 'running' | 'completed' | 'failed' | string;
  progress: number;
  startedMs: number;
  error: string | null;
}

export interface RuntimeChannelState {
  index: number;
  port: string;
  configured: boolean;
  enabled: boolean;
  owner: string;
  fault: boolean;
  brightness?: number;
  pixels?: number;
  level?: number;
}

export interface RuntimeSnapshotV3 {
  schema: 3;
  streamSeq: number;
  generatedAtMs: number;

  node: RuntimeNodeState;
  runtime: RuntimeState;
  output: OutputState;
  scene: SceneRuntimeState;
  manual: ManualRuntimeState;
  transport: TransportRuntimeState;
  sync: SyncRuntimeState;
  network: NetworkRuntimeState;
  protocols: ProtocolRuntimeState;
  timeline: TimelineRuntimeState;
  automation: AutomationRuntimeState;
  job: OperationJob | null;

  channels: RuntimeChannelState[];
}
