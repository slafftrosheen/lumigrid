import { apiJson } from './transport';
import { checkManifestCompatibility, evaluateSyncTopology } from './syncPolicy';
export { syncProtocolCompatibility, evaluateClusterReadiness, rememberSyncGroup, inSelectedSyncGroup } from './syncPolicy';

export type SyncRole = 'auto' | 'master' | 'worker';

export interface SyncPeer {
  address: string;
  ageMs: number;
  latencyMs: number;
}

export interface SyncStatus {
  protocolVersion?: number;
  syncProtocol?: number;
  syncEnabled?: boolean;
  groupLabel?: string;

  configuredRole: SyncRole;
  activeRole: SyncRole;
  running: boolean;

  clockLocked: boolean;
  clockHasEpoch: boolean;

  masterAddress: string | null;

  peerCount: number;
  lastPacketAgeMs: number | null;

  packetsReceived: number;
  packetsMissed: number;

  clockOffsetMs: number;
  clockJitterUs: number;

  transport: 'standalone' | 'scene' | 'timeline' | 'cue';
  transportGeneration?: number;
  transportApplyOk?: boolean;
  transportError?: string;

  remoteBpm: number | null;
  remoteTimelineEnabled: boolean;

  cue?: {
    listIndex: number;
    state: string;
    cueIndex: number;
    nextCueIndex: number;
    manualFade: number;
  };

  peers: SyncPeer[];
}

export interface ShowManifestScene {
  index: number;
  name?: string;
  checksum: number;
  motionChecksum?: number;
}

export interface ShowManifestCueList {
  index: number;
  name?: string;
  cueCount?: number;
  checksum: number;
}

export interface ShowManifest {
  schema?: number;
  schemaVersion?: number;
  firmware?: string;
  protocol?: number;
  syncProtocol?: number;
  playbackHash?: number;
  scheduleHash?: number;
  showHash: number;
  scenes: ShowManifestScene[];
  timeline?: {
    enabled?: boolean;
    bpm?: number;
    checksum: number;
  };
  schedules?: {
    checksum: number;
  };
  calendar?: {
    checksum: number;
  };
  cueLists?: ShowManifestCueList[];
}

export type ReachabilityState =
  | 'ONLINE'
  | 'ONLINE / SYNC DISABLED'
  | 'ONLINE / SYNC INCOMPATIBLE'
  | 'ONLINE / MANIFEST UNAVAILABLE'
  | 'OFFLINE';

export type ParityStatus = 'MATCHED' | 'DRIFT' | 'INCOMPATIBLE' | 'UNAVAILABLE';

export interface ShowDiffItem {
  category: 'scene' | 'sceneMotion' | 'timeline' | 'schedules' | 'cueList';
  index?: number;
  label: string;
  state: 'match' | 'changed' | 'missing' | 'extra';
}

export interface ShowParityResult {
  playbackStatus?: ParityStatus;
  scheduleStatus?: ParityStatus;
  status: ParityStatus;
  reason?: string;
  diffs: ShowDiffItem[];
}

/** Fetches runtime NodeSync status from a node. */
export async function getSyncStatus(ip: string): Promise<SyncStatus> {
  return apiJson<SyncStatus>(ip, '/api/v1/sync');
}

/** Fetches lightweight show manifest from a node. */
export async function getShowManifest(ip: string): Promise<ShowManifest> {
  return apiJson<ShowManifest>(ip, '/api/v1/show-manifest');
}

/**
 * Generates a cryptographically strong 32-bit unsigned sync token.
 * Never returns 0. Throws if Web Crypto is unavailable.
 */
export function generateSyncToken(): number {
  if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
    const values = new Uint32Array(1);
    crypto.getRandomValues(values);
    return values[0] || 1;
  }
  throw new Error('Secure random generation unavailable in this browser. Sync group configuration cannot continue.');
}

/**
 * Compares two ShowManifests to evaluate content parity and identify specific differences.
 */
export function compareShowManifest(
  source: ShowManifest | null | undefined,
  target: ShowManifest | null | undefined
): ShowParityResult {
  const compatibilityFailure = checkManifestCompatibility(source, target);
  if (compatibilityFailure) return compatibilityFailure;
  if (!source || !target) {
    return {
      playbackStatus: 'UNAVAILABLE',
      scheduleStatus: 'UNAVAILABLE',
      status: 'UNAVAILABLE',
      reason: 'Manifest data unavailable',
      diffs: [],
    };
  }

  // Check schema / protocol compatibility
  const sourceSchema = source.schema ?? source.schemaVersion ?? 1;
  const targetSchema = target.schema ?? target.schemaVersion ?? 1;
  if (sourceSchema !== targetSchema) {
    return {
      playbackStatus: 'INCOMPATIBLE',
      scheduleStatus: 'INCOMPATIBLE',
      status: 'INCOMPATIBLE',
      reason: `Schema version mismatch: source schema ${sourceSchema} vs target ${targetSchema}`,
      diffs: [],
    };
  }

  if (source.protocol !== undefined && target.protocol !== undefined && source.protocol !== target.protocol) {
    return {
      playbackStatus: 'INCOMPATIBLE',
      scheduleStatus: 'INCOMPATIBLE',
      status: 'INCOMPATIBLE',
      reason: `Protocol version mismatch: source ${source.protocol} vs target ${target.protocol}`,
      diffs: [],
    };
  }

  const diffs: ShowDiffItem[] = [];

  // Scenes & Motion diff
  const sourceScenes = new Map(source.scenes.map((s) => [s.index, s]));
  const targetScenes = new Map(target.scenes.map((s) => [s.index, s]));

  for (const [idx, sScene] of sourceScenes.entries()) {
    const tScene = targetScenes.get(idx);
    const label = sScene.name ? `Scene ${String(idx).padStart(2, '0')} (${sScene.name})` : `Scene ${String(idx).padStart(2, '0')}`;
    if (!tScene) {
      diffs.push({ category: 'scene', index: idx, label, state: 'missing' });
    } else if (tScene.checksum !== sScene.checksum) {
      diffs.push({ category: 'scene', index: idx, label, state: 'changed' });
    } else {
      diffs.push({ category: 'scene', index: idx, label, state: 'match' });
    }

    // Motion
    const sMotionCs = sScene.motionChecksum ?? 0;
    const tMotionCs = tScene?.motionChecksum ?? 0;
    if (sMotionCs !== 0 || tMotionCs !== 0) {
      const motionLabel = `Scene Motion ${String(idx).padStart(2, '0')}`;
      if (sMotionCs !== 0 && tMotionCs === 0) {
        diffs.push({ category: 'sceneMotion', index: idx, label: motionLabel, state: 'missing' });
      } else if (sMotionCs !== tMotionCs) {
        diffs.push({ category: 'sceneMotion', index: idx, label: motionLabel, state: 'changed' });
      } else {
        diffs.push({ category: 'sceneMotion', index: idx, label: motionLabel, state: 'match' });
      }
    }
  }

  for (const [idx, tScene] of targetScenes.entries()) {
    if (!sourceScenes.has(idx)) {
      const label = tScene.name ? `Scene ${String(idx).padStart(2, '0')} (${tScene.name})` : `Scene ${String(idx).padStart(2, '0')}`;
      diffs.push({ category: 'scene', index: idx, label, state: 'extra' });
    }
  }

  // Timeline
  const sTimelineCs = source.timeline?.checksum ?? 0;
  const tTimelineCs = target.timeline?.checksum ?? 0;
  diffs.push({
    category: 'timeline',
    label: 'Timeline Sequence',
    state: sTimelineCs === tTimelineCs ? 'match' : 'changed',
  });

  // Schedules / Calendar
  const sSchedCs = source.schedules?.checksum ?? source.calendar?.checksum ?? 0;
  const tSchedCs = target.schedules?.checksum ?? target.calendar?.checksum ?? 0;
  diffs.push({
    category: 'schedules',
    label: 'Calendar Schedules',
    state: sSchedCs === tSchedCs ? 'match' : 'changed',
  });

  // Cue Lists
  const sourceCues = new Map((source.cueLists ?? []).map((c) => [c.index, c]));
  const targetCues = new Map((target.cueLists ?? []).map((c) => [c.index, c]));

  for (const [idx, sCue] of sourceCues.entries()) {
    const tCue = targetCues.get(idx);
    const label = sCue.name ? `Cue List ${String(idx).padStart(2, '0')} (${sCue.name})` : `Cue List ${String(idx).padStart(2, '0')}`;
    if (!tCue) {
      diffs.push({ category: 'cueList', index: idx, label, state: 'missing' });
    } else if (tCue.checksum !== sCue.checksum) {
      diffs.push({ category: 'cueList', index: idx, label, state: 'changed' });
    } else {
      diffs.push({ category: 'cueList', index: idx, label, state: 'match' });
    }
  }

  for (const [idx, tCue] of targetCues.entries()) {
    if (!sourceCues.has(idx)) {
      const label = tCue.name ? `Cue List ${String(idx).padStart(2, '0')} (${tCue.name})` : `Cue List ${String(idx).padStart(2, '0')}`;
      diffs.push({ category: 'cueList', index: idx, label, state: 'extra' });
    }
  }

  const playbackDiffs = diffs.filter((d) => d.category !== 'schedules');
  const scheduleDiffs = diffs.filter((d) => d.category === 'schedules');
  const pStatus: ParityStatus = (playbackDiffs.some((d) => d.state !== 'match') ||
    (source.playbackHash !== undefined && target.playbackHash !== undefined && source.playbackHash !== target.playbackHash)) ? 'DRIFT' : 'MATCHED';
  const sStatus: ParityStatus = (scheduleDiffs.some((d) => d.state !== 'match') ||
    (source.scheduleHash !== undefined && target.scheduleHash !== undefined && source.scheduleHash !== target.scheduleHash)) ? 'DRIFT' : 'MATCHED';
  const overallStatus: ParityStatus = (pStatus === 'MATCHED' && sStatus === 'MATCHED') ? 'MATCHED' : 'DRIFT';

  return {
    playbackStatus: pStatus,
    scheduleStatus: sStatus,
    status: overallStatus,
    reason: overallStatus === 'DRIFT' ? (pStatus === 'DRIFT' ? 'Playback content differs' : 'Schedule content differs') : undefined,
    diffs,
  };
}

export interface NodeClusterPlan {
  syncEnabled: boolean;
  syncRole: number;
  syncToken: number;
  groupLabel?: string;
}

/**
 * Validates and builds cluster sync configuration plans for a set of nodes.
 */
export function buildClusterPlan(
  nodeIps: string[],
  mode: 'auto' | 'explicit',
  leaderIp?: string,
  groupLabel?: string,
  existingToken?: number
): { plan: Record<string, NodeClusterPlan>; error?: string } {
  if (nodeIps.length === 0) {
    return { plan: {}, error: 'No nodes selected for sync group' };
  }

  const syncToken = existingToken && existingToken > 0 ? existingToken : generateSyncToken();

  if (mode === 'auto') {
    const plan: Record<string, NodeClusterPlan> = {};
    for (const ip of nodeIps) {
      plan[ip] = {
        syncEnabled: true,
        syncRole: 0, // Auto
        syncToken,
        groupLabel,
      };
    }
    return { plan };
  }

  if (mode === 'explicit') {
    if (!leaderIp) {
      return { plan: {}, error: 'Explicit leader mode requires a selected leader node' };
    }
    if (!nodeIps.includes(leaderIp)) {
      return { plan: {}, error: `Selected leader ${leaderIp} must be a member of the sync group` };
    }

    const plan: Record<string, NodeClusterPlan> = {};
    for (const ip of nodeIps) {
      plan[ip] = {
        syncEnabled: true,
        syncRole: ip === leaderIp ? 1 : 2, // 1 = Master, 2 = Worker
        syncToken,
        groupLabel,
      };
    }
    return { plan };
  }

  return { plan: {}, error: `Unknown mode ${mode}` };
}

/**
 * Compares only playback assets (Scenes, Scene Motion, Timeline content, Cue Lists)
 * ignoring local calendar schedules.
 */
export function comparePlaybackManifest(
  source: ShowManifest | null | undefined,
  target: ShowManifest | null | undefined
): ShowParityResult {
  const compatibilityFailure = checkManifestCompatibility(source, target);
  if (compatibilityFailure) return compatibilityFailure;
  if (!source || !target) {
    return {
      playbackStatus: 'UNAVAILABLE',
      scheduleStatus: 'UNAVAILABLE',
      status: 'UNAVAILABLE',
      reason: 'Manifest data unavailable',
      diffs: [],
    };
  }

  const diffs: ShowDiffItem[] = [];

  // Scenes & Motion diff
  const sourceScenes = new Map(source.scenes.map((s) => [s.index, s]));
  const targetScenes = new Map(target.scenes.map((s) => [s.index, s]));

  for (const [idx, sScene] of sourceScenes.entries()) {
    const tScene = targetScenes.get(idx);
    const label = sScene.name ? `Scene ${String(idx).padStart(2, '0')} (${sScene.name})` : `Scene ${String(idx).padStart(2, '0')}`;
    if (!tScene) {
      diffs.push({ category: 'scene', index: idx, label, state: 'missing' });
    } else if (tScene.checksum !== sScene.checksum) {
      diffs.push({ category: 'scene', index: idx, label, state: 'changed' });
    } else {
      diffs.push({ category: 'scene', index: idx, label, state: 'match' });
    }

    const sMotionCs = sScene.motionChecksum ?? 0;
    const tMotionCs = tScene?.motionChecksum ?? 0;
    if (sMotionCs !== 0 || tMotionCs !== 0) {
      const motionLabel = `Scene Motion ${String(idx).padStart(2, '0')}`;
      if (sMotionCs !== 0 && tMotionCs === 0) {
        diffs.push({ category: 'sceneMotion', index: idx, label: motionLabel, state: 'missing' });
      } else if (sMotionCs !== tMotionCs) {
        diffs.push({ category: 'sceneMotion', index: idx, label: motionLabel, state: 'changed' });
      } else {
        diffs.push({ category: 'sceneMotion', index: idx, label: motionLabel, state: 'match' });
      }
    }
  }

  for (const [idx, tScene] of targetScenes.entries()) {
    if (!sourceScenes.has(idx)) {
      const label = tScene.name ? `Scene ${String(idx).padStart(2, '0')} (${tScene.name})` : `Scene ${String(idx).padStart(2, '0')}`;
      diffs.push({ category: 'scene', index: idx, label, state: 'extra' });
    }
  }

  // Timeline
  const sTimelineCs = source.timeline?.checksum ?? 0;
  const tTimelineCs = target.timeline?.checksum ?? 0;
  diffs.push({
    category: 'timeline',
    label: 'Timeline Sequence',
    state: sTimelineCs === tTimelineCs ? 'match' : 'changed',
  });

  // Cue Lists
  const sourceCues = new Map((source.cueLists ?? []).map((c) => [c.index, c]));
  const targetCues = new Map((target.cueLists ?? []).map((c) => [c.index, c]));

  for (const [idx, sCue] of sourceCues.entries()) {
    const tCue = targetCues.get(idx);
    const label = sCue.name ? `Cue List ${String(idx).padStart(2, '0')} (${sCue.name})` : `Cue List ${String(idx).padStart(2, '0')}`;
    if (!tCue) {
      diffs.push({ category: 'cueList', index: idx, label, state: 'missing' });
    } else if (tCue.checksum !== sCue.checksum) {
      diffs.push({ category: 'cueList', index: idx, label, state: 'changed' });
    } else {
      diffs.push({ category: 'cueList', index: idx, label, state: 'match' });
    }
  }

  for (const [idx, tCue] of targetCues.entries()) {
    if (!sourceCues.has(idx)) {
      const label = tCue.name ? `Cue List ${String(idx).padStart(2, '0')} (${tCue.name})` : `Cue List ${String(idx).padStart(2, '0')}`;
      diffs.push({ category: 'cueList', index: idx, label, state: 'extra' });
    }
  }

  if (source.playbackHash !== undefined && target.playbackHash !== undefined) {
    if (source.playbackHash === target.playbackHash && !diffs.some(d => d.state !== 'match')) {
      return { playbackStatus: 'MATCHED', scheduleStatus: 'MATCHED', status: 'MATCHED', diffs };
    }
    return {
      playbackStatus: 'DRIFT',
      scheduleStatus: 'MATCHED',
      status: 'DRIFT',
      reason: source.playbackHash === target.playbackHash
        ? 'Playback records differ despite matching aggregate hashes'
        : `Playback content differs (hash ${source.playbackHash} vs ${target.playbackHash})`,
      diffs,
    };
  }

  const hasDrift = diffs.some((d) => d.state !== 'match');
  const pStatus: ParityStatus = hasDrift ? 'DRIFT' : 'MATCHED';
  return {
    playbackStatus: pStatus,
    scheduleStatus: 'MATCHED',
    status: pStatus,
    reason: hasDrift ? 'Playback content differs' : undefined,
    diffs,
  };
}

/**
 * Compares only schedule/calendar assets.
 */
export function compareScheduleManifest(
  source: ShowManifest | null | undefined,
  target: ShowManifest | null | undefined
): ShowParityResult {
  const compatibilityFailure = checkManifestCompatibility(source, target);
  if (compatibilityFailure) return compatibilityFailure;
  if (!source || !target) {
    return {
      playbackStatus: 'UNAVAILABLE',
      scheduleStatus: 'UNAVAILABLE',
      status: 'UNAVAILABLE',
      reason: 'Manifest data unavailable',
      diffs: [],
    };
  }

  const sSchedCs = source.schedules?.checksum ?? source.calendar?.checksum ?? 0;
  const tSchedCs = target.schedules?.checksum ?? target.calendar?.checksum ?? 0;
  const isDiff = sSchedCs !== tSchedCs ||
    (source.scheduleHash !== undefined && target.scheduleHash !== undefined && source.scheduleHash !== target.scheduleHash);

  const diffs: ShowDiffItem[] = [
    {
      category: 'schedules',
      label: 'Calendar Schedules',
      state: !isDiff ? 'match' : 'changed',
    },
  ];

  const schedStatus: ParityStatus = !isDiff ? 'MATCHED' : 'DRIFT';

  return {
    playbackStatus: 'MATCHED',
    scheduleStatus: schedStatus,
    status: schedStatus,
    reason: isDiff ? 'Schedule content differs' : undefined,
    diffs,
  };
}

/**
 * Compares full show manifest including schedules.
 */
export function compareFullShowManifest(
  source: ShowManifest | null | undefined,
  target: ShowManifest | null | undefined
): ShowParityResult {
  return compareShowManifest(source, target);
}

export type ClusterTopologyState =
  | 'NO_LEADER'
  | 'VALID_LEADER'
  | 'SPLIT_BRAIN'
  | 'LEADER_DISAGREEMENT';

export interface ClusterTopology {
  masterCount: number;
  leaderIp: string | null;
  state: ClusterTopologyState;
  error?: string;
  isReady: boolean;
}

/**
 * Evaluates real cluster topology truth from reported node statuses.
 * NEVER infers authority from selected or first node.
 */
export function evaluateClusterTopology(
  nodes: { ip: string; status?: SyncStatus | null }[]
): ClusterTopology {
  return evaluateSyncTopology(nodes);
}
