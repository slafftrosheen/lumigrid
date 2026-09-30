import type { RuntimeSnapshotV3, RuntimeChannelState } from '../runtimeSnapshot';

export class InvalidRuntimeSnapshotError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'InvalidRuntimeSnapshotError';
  }
}

/**
 * Normalizes raw payload from either Schema 3 (authoritative) or Schema 2 (compatibility).
 * Rejects unknown or malformed schemas.
 */
export function normalizeRuntimeSnapshot(raw: unknown): RuntimeSnapshotV3 {
  if (typeof raw !== 'object' || raw === null) {
    throw new InvalidRuntimeSnapshotError('Raw runtime snapshot is null or not an object');
  }

  const data = raw as Record<string, any>;

  // 1. Schema 3: Canonical authoritative state
  if (data.schema === 3) {
    if (!data.node || typeof data.node.id !== 'string') {
      throw new InvalidRuntimeSnapshotError('Schema 3 snapshot missing valid node.id');
    }
    if (!data.runtime || typeof data.runtime.brightness !== 'number') {
      throw new InvalidRuntimeSnapshotError('Schema 3 snapshot missing runtime object');
    }
    if (!data.output) {
      throw new InvalidRuntimeSnapshotError('Schema 3 snapshot missing output object');
    }
    if (!data.scene) {
      throw new InvalidRuntimeSnapshotError('Schema 3 snapshot missing scene object');
    }
    if (!Array.isArray(data.channels)) {
      throw new InvalidRuntimeSnapshotError('Schema 3 snapshot missing channels array');
    }

    return {
      schema: 3,
      streamSeq: Number(data.streamSeq) || 0,
      generatedAtMs: Number(data.generatedAtMs) || Date.now(),
      node: {
        id: String(data.node.id),
      },
      runtime: {
        effect: String(data.runtime.effect || 'solid'),
        brightness: Number(data.runtime.brightness) || 0,
        testActive: Boolean(data.runtime.testActive),
        autoLoop: Boolean(data.runtime.autoLoop),
        controlOwner: String(data.runtime.controlOwner || 'Unknown'),
        frameCount: Number(data.runtime.frameCount) || 0,
      },
      output: {
        enabled: data.output.enabled !== undefined ? Boolean(data.output.enabled) : true,
        powerMw: Number(data.output.powerMw) || 0,
        state: String(data.output.state || (data.output.fault ? 'fault' : 'healthy')),
        fault: Boolean(data.output.fault),
        faultCount: Number(data.output.faultCount) || 0,
        estopActive: Boolean(data.output.estopActive),
      },
      scene: {
        activePreset: Number(data.scene.activePreset) || 0,
        requestedPreset: Number(data.scene.requestedPreset) || 0,
        requestedGeneration: Number(data.scene.requestedGeneration) || 0,
        appliedGeneration: Number(data.scene.appliedGeneration) || 0,
        applyPending: Boolean(data.scene.applyPending),
        transitioning: Boolean(data.scene.transitioning),
        fallbackActive: Boolean(data.scene.fallbackActive),
      },
      manual: {
        active: Boolean(data.manual?.active),
        channelMask: Number(data.manual?.channelMask) || 0,
        leaseRemainingMs: Number(data.manual?.leaseRemainingMs) || 0,
      },
      transport: {
        mode: String(data.transport?.mode || 'scene'),
        generation: Number(data.transport?.generation) || 0,
        sceneIndex: Number(data.transport?.sceneIndex) || 0,
        timeline: {
          running: Boolean(data.transport?.timeline?.running),
          bpm: Number(data.transport?.timeline?.bpm) || 120,
        },
        cue: {
          running: Boolean(data.transport?.cue?.running),
          state: data.transport?.cue?.state,
          listIndex: data.transport?.cue?.listIndex,
          cueIndex: data.transport?.cue?.cueIndex,
          nextCueIndex: data.transport?.cue?.nextCueIndex,
          manualFade: data.transport?.cue?.manualFade,
        },
      },
      sync: {
        configuredRole: String(data.sync?.configuredRole || 'auto'),
        activeRole: String(data.sync?.activeRole || 'auto'),
        clockLocked: Boolean(data.sync?.clockLocked),
        masterAddress: data.sync?.masterAddress || undefined,
        lastPacketAgeMs: Number(data.sync?.lastPacketAgeMs) || 0,
        clockOffsetMs: Number(data.sync?.clockOffsetMs) || 0,
        clockJitterUs: Number(data.sync?.clockJitterUs) || 0,
      },
      network: {
        state: String(data.network?.state || 'connected'),
        rssi: Number(data.network?.rssi) || 0,
        staConnected: Boolean(data.network?.staConnected),
        staIp: String(data.network?.staIp || ''),
        apActive: Boolean(data.network?.apActive),
        applyState: String(data.network?.applyState || 'idle'),
      },
      protocols: {
        artnet: {
          enabled: Boolean(data.protocols?.artnet?.enabled ?? true),
          sources: Number(data.protocols?.artnet?.sources) || 0,
          packetsPerSecond: Number(data.protocols?.artnet?.packetsPerSecond) || 0,
          lastPacketAgeMs: Number(data.protocols?.artnet?.lastPacketAgeMs) || 0,
        },
        sacn: {
          enabled: Boolean(data.protocols?.sacn?.enabled ?? true),
          sources: Number(data.protocols?.sacn?.sources) || 0,
          packetsPerSecond: Number(data.protocols?.sacn?.packetsPerSecond) || 0,
        },
      },
      timeline: {
        clockMs: Number(data.timeline?.clockMs) || 0,
        driftPpm: Number(data.timeline?.driftPpm) || 0,
        synced: Boolean(data.timeline?.synced),
      },
      automation: {
        activeTracks: Number(data.automation?.activeTracks) || 0,
        transitionCurve: String(data.automation?.transitionCurve || 'easeInOut'),
      },
      job: data.job
        ? {
            jobId: Number(data.job.jobId) || 0,
            type: String(data.job.type || 'unknown'),
            state: String(data.job.state || 'idle'),
            progress: Number(data.job.progress) || 0,
            startedMs: Number(data.job.startedMs) || 0,
            error: data.job.error ? String(data.job.error) : null,
          }
        : null,
      channels: data.channels.map((ch: any, idx: number) => ({
        index: Number(ch.index ?? idx),
        port: String(ch.port || `CH${idx + 1}`),
        configured: Boolean(ch.configured),
        enabled: Boolean(ch.enabled),
        owner: String(ch.owner || 'Unknown'),
        fault: Boolean(ch.fault),
        brightness: ch.brightness !== undefined ? Number(ch.brightness) : undefined,
        pixels: ch.pixels !== undefined ? Number(ch.pixels) : undefined,
        level: ch.level !== undefined ? Number(ch.level) : undefined,
      })),
    };
  }

  // 2. Schema 2: Explicit mapping from legacy schema 2 or legacy unversioned status frames
  if (
    data.schema === 2 ||
    (data.schema === undefined &&
      (data.effect !== undefined ||
        data.brightness !== undefined ||
        data.outputsEnabled !== undefined ||
        data.state !== undefined))
  ) {
    const st = data.state || {};
    const rt = st.runtime || {};
    const out = st.output || {};
    const sc = st.scene || {};
    const man = data.manual || st.manual || {};
    const tr = data.showTransport || st.showTransport || {};
    const sync = st.sync || {};
    const net = st.network || {};
    const proto = st.protocol || {};
    const tl = st.timeline || {};
    const auto = st.automation || {};
    const j = st.job || data.job;

    const channels: RuntimeChannelState[] = Array.isArray(data.channels)
      ? data.channels.map((ch: any, idx: number) => ({
          index: Number(ch.index ?? idx),
          port: String(ch.port || `CH${idx + 1}`),
          configured: Boolean(ch.configured),
          enabled: Boolean(ch.enabled),
          owner: String(ch.owner || 'Unknown'),
          fault: Boolean(ch.fault),
          brightness: ch.brightness !== undefined ? Number(ch.brightness) : undefined,
          pixels: ch.pixels !== undefined ? Number(ch.pixels) : undefined,
          level: ch.level !== undefined ? Number(ch.level) : undefined,
        }))
      : [];

    return {
      schema: 3,
      streamSeq: Number(data.seq) || 0,
      generatedAtMs: Number(data.uptimeMs) || Date.now(),
      node: {
        id: String(data.nodeId || 'unknown'),
      },
      runtime: {
        effect: String(rt.effect || data.effect || 'solid'),
        brightness: Number(rt.brightness ?? data.brightness ?? 0),
        testActive: Boolean(rt.testActive ?? data.testActive),
        autoLoop: Boolean(rt.autoLoop ?? data.autoLoop),
        controlOwner: String(rt.controlOwner || rt.owner || data.controlOwner || 'Unknown'),
        frameCount: Number(rt.frameCount) || 0,
      },
      output: {
        enabled: Boolean(out.outputsEnabled ?? data.outputsEnabled ?? true),
        powerMw: Number(out.powerMw) || 0,
        state: out.fault ? 'fault' : 'healthy',
        fault: Boolean(out.fault),
        faultCount: Number(out.faultCount) || 0,
        estopActive: false,
      },
      scene: {
        activePreset: Number(sc.activePreset ?? data.activePreset ?? 0),
        requestedPreset: Number(sc.requestedPreset ?? 0),
        requestedGeneration: Number(sc.requestedGeneration ?? 0),
        appliedGeneration: Number(sc.appliedGeneration ?? data.appliedGeneration ?? 0),
        applyPending: Boolean(sc.sceneApplyPending),
        transitioning: Boolean(sc.transitioning),
        fallbackActive: false,
      },
      manual: {
        active: Boolean(man.active),
        channelMask: Number(man.channelMask) || 0,
        leaseRemainingMs: Number(man.leaseRemainingMs) || 0,
      },
      transport: {
        mode: String(tr.mode || 'scene'),
        generation: Number(tr.generation) || 0,
        sceneIndex: Number(tr.sceneIndex) || 0,
        timeline: {
          running: Boolean(tr.timelineRunning),
          bpm: Number(tr.timelineBpm) || 120,
        },
        cue: {
          running: Boolean(tr.cue?.running),
          state: tr.cue?.state,
          listIndex: tr.cue?.listIndex,
          cueIndex: tr.cue?.cueIndex,
          nextCueIndex: tr.cue?.nextCueIndex,
          manualFade: tr.cue?.manualFade,
        },
      },
      sync: {
        configuredRole: String(sync.configuredRole || sync.role || 'auto'),
        activeRole: String(sync.activeRole || sync.role || 'auto'),
        clockLocked: Boolean(sync.inSync ?? sync.clockLocked),
        masterAddress: sync.masterAddress,
        lastPacketAgeMs: Number(sync.lastPacketAgeMs) || 0,
        clockOffsetMs: Number(sync.clockOffsetMs) || 0,
        clockJitterUs: Number(sync.clockJitterUs) || 0,
      },
      network: {
        state: 'connected',
        rssi: Number(net.rssi) || 0,
        staConnected: true,
        staIp: '',
        apActive: false,
        applyState: 'idle',
      },
      protocols: {
        artnet: {
          enabled: Boolean(proto.artnet?.enabled ?? true),
          sources: Number(proto.artnet?.sources) || 0,
          packetsPerSecond: Number(proto.artnet?.packetsPerSecond) || 0,
          lastPacketAgeMs: Number(proto.artnet?.lastPacketAgeMs) || 0,
        },
        sacn: {
          enabled: Boolean(proto.sacn?.enabled ?? true),
          sources: Number(proto.sacn?.sources) || 0,
          packetsPerSecond: Number(proto.sacn?.packetsPerSecond) || 0,
        },
      },
      timeline: {
        clockMs: Number(tl.clockMs) || 0,
        driftPpm: Number(tl.driftPpm) || 0,
        synced: Boolean(tl.synced),
      },
      automation: {
        activeTracks: Number(auto.activeTracks) || 0,
        transitionCurve: String(auto.transitionCurve || 'easeInOut'),
      },
      job: j
        ? {
            jobId: Number(j.jobId) || 0,
            type: String(j.type || 'unknown'),
            state: String(j.state || 'idle'),
            progress: Number(j.progress) || 0,
            startedMs: Number(j.startedMs) || 0,
            error: j.error ? String(j.error) : null,
          }
        : null,
      channels,
    };
  }

  throw new InvalidRuntimeSnapshotError(
    `Unsupported runtime snapshot schema: ${data.schema !== undefined ? String(data.schema) : 'missing'}`
  );
}
