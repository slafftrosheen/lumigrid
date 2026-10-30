import type { ClusterTopology, ShowManifest, ShowParityResult, SyncStatus } from './sync';

// Matches CapabilitiesManifest::kProtocolSync and NodeSync::kPacketVersion.
// Wire compatibility is separate from the show-manifest envelope protocol (1).
export const SUPPORTED_SYNC_PROTOCOL = 6;
export const SYNC_FRESHNESS_MS = 3000;

type VersionFields = { protocolVersion?: unknown; syncProtocol?: unknown };
export interface SyncCompatibility {
  state: 'supported' | 'unknown' | 'unsupported' | 'conflicting';
  version: number | null;
  reason: string;
}

export function syncProtocolCompatibility(status: VersionFields | null | undefined): SyncCompatibility {
  const versions = [status?.protocolVersion, status?.syncProtocol].filter(v => v !== undefined);
  if (!versions.length || versions.some(v => typeof v !== 'number' || !Number.isInteger(v) || v < 1)) {
    return { state: 'unknown', version: null, reason: 'Sync version was not reported. Refresh or update this node.' };
  }
  const version = versions[0] as number;
  if (versions.some(v => v !== version)) {
    return { state: 'conflicting', version: null, reason: 'The node reports conflicting sync versions.' };
  }
  if (version !== SUPPORTED_SYNC_PROTOCOL) {
    return { state: 'unsupported', version, reason: 'Update all group members to a compatible software version.' };
  }
  return { state: 'supported', version, reason: '' };
}

export interface SyncMember {
  ip: string;
  status?: SyncStatus | null;
}

/** Observed runtime state only; same labels do not prove shared group credentials. */
export function evaluateSyncTopology(nodes: readonly SyncMember[]): ClusterTopology {
  const masters = nodes.filter(n => n.status?.activeRole === 'master' &&
    n.status.running === true && n.status.syncEnabled === true);
  const masterCount = masters.length;
  if (!masterCount) return { masterCount, leaderIp:null, state:'NO_LEADER', isReady:false, error:'No running leader. Check group configuration and connectivity.' };
  if (masterCount > 1) return { masterCount, leaderIp:null, state:'SPLIT_BRAIN', isReady:false, error:'Multiple leaders are active. Select one group or resolve leader roles.' };
  const leaderIp = masters[0].ip;
  const result: ClusterTopology = { masterCount, leaderIp, state:'VALID_LEADER', isReady:false };
  const fail = (error: string): ClusterTopology => ({ ...result, error });
  if (nodes.some(n => !n.ip) || new Set(nodes.map(n => n.ip)).size !== nodes.length) {
    return fail('Each group member must have one unique node address.');
  }
  const leaderGroup = masters[0].status?.groupLabel ?? '';
  for (const node of nodes) {
    const status = node.status;
    if (!status) return fail(`${node.ip}: status is unavailable.`);
    const compatibility = syncProtocolCompatibility(status);
    if (compatibility.state !== 'supported') return fail(`${node.ip}: ${compatibility.reason}`);
    if (status.syncEnabled !== true || status.running !== true) return fail(`${node.ip}: synchronization is stopped or disabled.`);
    if ((status.groupLabel ?? '') !== leaderGroup) return fail('Select one sync group before starting group playback.');
    if (node.ip === leaderIp) continue; // The leader may use its local monotonic clock.
    if (status.activeRole !== 'worker') return fail(`${node.ip}: waiting for a follower role.`);
    if (status.masterAddress !== leaderIp) {
      return { ...result, state:'LEADER_DISAGREEMENT', error:`${node.ip}: not following the selected leader.` };
    }
    const age = status.lastPacketAgeMs;
    if (typeof age !== 'number' || !Number.isFinite(age) || age < 0 || age > SYNC_FRESHNESS_MS ||
        !Number.isInteger(status.packetsReceived) || status.packetsReceived < 1) {
      return fail(`${node.ip}: waiting for a fresh leader heartbeat.`);
    }
    if (status.clockLocked !== true) return fail(`${node.ip}: waiting for clock alignment.`);
    if (status.transportApplyOk !== true) return fail(`${node.ip}: playback has not been confirmed by the follower.`);
  }
  return { ...result, isReady:true };
}

export interface ReadinessMember extends SyncMember {
  online: boolean;
  playbackStatus?: string;
}
export function evaluateClusterReadiness(nodes: readonly ReadinessMember[]): { ready: boolean; reason: string } {
  if (!nodes.length) return { ready:false, reason:'Add a node or select a sync group.' };
  const missing = nodes.find(n => n.online !== true || !n.status);
  if (missing) return { ready:false, reason:`${missing.ip}: reconnect this group member.` };
  const topology = evaluateSyncTopology(nodes);
  if (!topology.isReady) return { ready:false, reason:topology.error ?? 'Group synchronization is not ready.' };
  const drift = nodes.find(n => n.playbackStatus !== 'MATCHED');
  if (drift) return { ready:false, reason:`${drift.ip}: compare and align scenes, cues and timeline before playback.` };
  return { ready:true, reason:'Group state is current and playback content matches. Physical timing is not measured here.' };
}

const record = (value: unknown): value is Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value);
const uint32 = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xffffffff;

function recordsValid(value: unknown, count: number, motion: boolean): boolean {
  if (!Array.isArray(value) || value.length > count) return false;
  const seen = new Set<number>();
  return value.every(item => {
    if (!record(item) || !uint32(item.index) || item.index >= count || seen.has(item.index) || !uint32(item.checksum)) return false;
    if (motion && item.motionChecksum !== undefined && !uint32(item.motionChecksum)) return false;
    seen.add(item.index); return true;
  });
}

/** Validate evidence before diffing it. Missing/invalid evidence is never MATCHED. */
export function checkManifestCompatibility(
  source: ShowManifest | null | undefined, target: ShowManifest | null | undefined,
): ShowParityResult | null {
  const fail = (status: 'INCOMPATIBLE' | 'UNAVAILABLE', reason: string): ShowParityResult =>
    ({ status, playbackStatus:status, scheduleStatus:status, reason, diffs:[] });
  for (const [label, input] of [['Source', source], ['Target', target]] as const) {
    if (!record(input)) return fail('UNAVAILABLE', `${label} manifest unavailable.`);
    const schema = input.schema ?? input.schemaVersion;
    if (input.schema !== undefined && input.schemaVersion !== undefined && input.schema !== input.schemaVersion) {
      return fail('INCOMPATIBLE', `${label} manifest schema fields disagree.`);
    }
    if (!uint32(schema) || !uint32(input.protocol) || !uint32(input.syncProtocol)) {
      return fail('UNAVAILABLE', `${label} manifest version evidence is missing or invalid.`);
    }
    if (schema !== 2) return fail('INCOMPATIBLE', `Schema version mismatch: ${label} schema ${schema}; expected 2.`);
    if (input.protocol !== 1) return fail('INCOMPATIBLE', `Protocol version mismatch: ${label} manifest protocol ${input.protocol}; expected 1.`);
    if (input.syncProtocol !== SUPPORTED_SYNC_PROTOCOL) return fail('INCOMPATIBLE', `Sync protocol version mismatch: ${label} sync ${input.syncProtocol}; expected ${SUPPORTED_SYNC_PROTOCOL}.`);
    if (!recordsValid(input.scenes, 32, true) ||
        (input.cueLists !== undefined && !recordsValid(input.cueLists, 8, false))) {
      return fail('UNAVAILABLE', `${label} manifest contains missing, invalid or duplicate content records.`);
    }
    for (const key of ['timeline', 'schedules', 'calendar'] as const) {
      const value = input[key];
      if (value !== undefined && (!record(value) || !uint32(value.checksum))) {
        return fail('UNAVAILABLE', `${label} ${key} evidence is invalid.`);
      }
    }
    for (const key of ['playbackHash', 'scheduleHash', 'showHash'] as const) {
      if (input[key] !== undefined && !uint32(input[key])) return fail('UNAVAILABLE', `${label} content hash is invalid.`);
    }
  }
  return null;
}

/** Membership is cached independently of live status so an offline follower
 * cannot disappear from a selected group's readiness denominator. */
export function rememberSyncGroup(current: unknown, previous?: string): string | undefined {
  return typeof current === 'string' ? current : previous;
}

/** Unknown membership blocks conservatively until it can be established. */
export function inSelectedSyncGroup(group: string | undefined, selected: string): boolean {
  return selected === 'All' || group === undefined || group === selected;
}
