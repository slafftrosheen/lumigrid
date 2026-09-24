import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  getSyncStatus,
  getShowManifest,
  generateSyncToken,
  compareShowManifest,
  comparePlaybackManifest,
  compareScheduleManifest,
  compareFullShowManifest,
  evaluateClusterTopology,
  buildClusterPlan,
  type ShowManifest,
  type SyncStatus,
} from './sync';
import * as transport from './transport';

describe('sync API module', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  describe('getSyncStatus', () => {
    it('fetches and returns sync status with extended fields', async () => {
      const mockStatus: SyncStatus = {
        configuredRole: 'worker',
        activeRole: 'worker',
        running: true,
        clockLocked: true,
        clockHasEpoch: true,
        masterAddress: '192.168.1.50',
        peerCount: 2,
        lastPacketAgeMs: 45,
        packetsReceived: 1024,
        packetsMissed: 2,
        clockOffsetMs: -1,
        clockJitterUs: 320,
        transport: 'timeline',
        remoteBpm: 128,
        remoteTimelineEnabled: true,
        peers: [
          { address: '192.168.1.50', ageMs: 45, latencyMs: 4 },
          { address: '192.168.1.52', ageMs: 120, latencyMs: 6 },
        ],
      };

      const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue(mockStatus);

      const res = await getSyncStatus('192.168.1.51');
      expect(spy).toHaveBeenCalledWith('192.168.1.51', '/api/v1/sync');
      expect(res.masterAddress).toBe('192.168.1.50');
      expect(res.transport).toBe('timeline');
      expect(res.remoteBpm).toBe(128);
      expect(res.remoteTimelineEnabled).toBe(true);
      expect(res.lastPacketAgeMs).toBe(45);
    });

    it('handles nullable masterAddress and lastPacketAgeMs for leader/idle nodes', async () => {
      const mockStatus: SyncStatus = {
        configuredRole: 'master',
        activeRole: 'master',
        running: true,
        clockLocked: true,
        clockHasEpoch: true,
        masterAddress: null,
        peerCount: 0,
        lastPacketAgeMs: null,
        packetsReceived: 0,
        packetsMissed: 0,
        clockOffsetMs: 0,
        clockJitterUs: 0,
        transport: 'standalone',
        remoteBpm: null,
        remoteTimelineEnabled: false,
        peers: [],
      };

      vi.spyOn(transport, 'apiJson').mockResolvedValue(mockStatus);

      const res = await getSyncStatus('192.168.1.50');
      expect(res.masterAddress).toBeNull();
      expect(res.lastPacketAgeMs).toBeNull();
      expect(res.remoteBpm).toBeNull();
      expect(res.transport).toBe('standalone');
    });
  });

  describe('getShowManifest', () => {
    it('fetches /api/v1/show-manifest', async () => {
      const mockManifest: ShowManifest = {
        schema: 1,
        firmware: '0.9.0',
        protocol: 1,
        showHash: 12345678,
        scenes: [{ index: 0, name: 'Brand Night', checksum: 111, motionChecksum: 222 }],
        timeline: { enabled: true, bpm: 120, checksum: 333 },
        schedules: { checksum: 444 },
        cueLists: [{ index: 0, name: 'Intro', cueCount: 4, checksum: 555 }],
      };

      const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue(mockManifest);

      const res = await getShowManifest('192.168.1.50');
      expect(spy).toHaveBeenCalledWith('192.168.1.50', '/api/v1/show-manifest');
      expect(res.showHash).toBe(12345678);
      expect(res.scenes.length).toBe(1);
    });
  });

  describe('generateSyncToken', () => {
    it('generates a non-zero number', () => {
      const token = generateSyncToken();
      expect(typeof token).toBe('number');
      expect(token).toBeGreaterThan(0);
    });
  });

  describe('compareShowManifest', () => {
    const baseManifest: ShowManifest = {
      schema: 1,
      protocol: 1,
      showHash: 1000,
      scenes: [
        { index: 0, checksum: 10, motionChecksum: 100 },
        { index: 1, checksum: 20, motionChecksum: 0 },
      ],
      timeline: { checksum: 30 },
      schedules: { checksum: 40 },
      cueLists: [{ index: 0, checksum: 50 }],
    };

    it('returns UNAVAILABLE if either manifest is missing', () => {
      expect(compareShowManifest(null, baseManifest).status).toBe('UNAVAILABLE');
      expect(compareShowManifest(baseManifest, undefined).status).toBe('UNAVAILABLE');
    });

    it('returns INCOMPATIBLE if schemas differ', () => {
      const target = { ...baseManifest, schema: 2 };
      const res = compareShowManifest(baseManifest, target);
      expect(res.status).toBe('INCOMPATIBLE');
      expect(res.reason).toContain('Schema version mismatch');
    });

    it('returns INCOMPATIBLE if protocols differ', () => {
      const target = { ...baseManifest, protocol: 2 };
      const res = compareShowManifest(baseManifest, target);
      expect(res.status).toBe('INCOMPATIBLE');
      expect(res.reason).toContain('Protocol version mismatch');
    });

    it('returns MATCHED when showHash and content match', () => {
      const target = { ...baseManifest };
      const res = compareShowManifest(baseManifest, target);
      expect(res.status).toBe('MATCHED');
      expect(res.diffs.every((d) => d.state === 'match')).toBe(true);
    });

    it('returns DRIFT with detailed diffs when content differs', () => {
      const target: ShowManifest = {
        schema: 1,
        protocol: 1,
        showHash: 2000, // Differs
        scenes: [
          { index: 0, checksum: 10, motionChecksum: 999 }, // Motion changed
          { index: 2, checksum: 77, motionChecksum: 0 }, // Scene 1 missing, Scene 2 extra
        ],
        timeline: { checksum: 30 }, // match
        schedules: { checksum: 99 }, // changed
        cueLists: [], // Cue 0 missing
      };

      const res = compareShowManifest(baseManifest, target);
      expect(res.status).toBe('DRIFT');

      const scene0Motion = res.diffs.find((d) => d.category === 'sceneMotion' && d.index === 0);
      expect(scene0Motion?.state).toBe('changed');

      const scene1 = res.diffs.find((d) => d.category === 'scene' && d.index === 1);
      expect(scene1?.state).toBe('missing');

      const scene2 = res.diffs.find((d) => d.category === 'scene' && d.index === 2);
      expect(scene2?.state).toBe('extra');

      const sched = res.diffs.find((d) => d.category === 'schedules');
      expect(sched?.state).toBe('changed');

      const cue0 = res.diffs.find((d) => d.category === 'cueList' && d.index === 0);
      expect(cue0?.state).toBe('missing');
    });
  });

  describe('buildClusterPlan', () => {
    const nodes = ['192.168.1.10', '192.168.1.11', '192.168.1.12'];

    it('builds automatic cluster plan with role 0 for all nodes', () => {
      const { plan, error } = buildClusterPlan(nodes, 'auto', undefined, 'Main Cluster', 9999);
      expect(error).toBeUndefined();
      expect(Object.keys(plan).length).toBe(3);
      for (const ip of nodes) {
        expect(plan[ip].syncEnabled).toBe(true);
        expect(plan[ip].syncRole).toBe(0);
        expect(plan[ip].syncToken).toBe(9999);
        expect(plan[ip].groupLabel).toBe('Main Cluster');
      }
    });

    it('builds explicit cluster plan with leader role 1 and followers role 2', () => {
      const { plan, error } = buildClusterPlan(nodes, 'explicit', '192.168.1.10', 'Main Cluster', 8888);
      expect(error).toBeUndefined();
      expect(plan['192.168.1.10'].syncRole).toBe(1); // Leader
      expect(plan['192.168.1.11'].syncRole).toBe(2); // Follower
      expect(plan['192.168.1.12'].syncRole).toBe(2); // Follower
      expect(plan['192.168.1.10'].syncToken).toBe(8888);
    });

    it('fails explicit plan if leader is not selected or not in cluster', () => {
      const r1 = buildClusterPlan(nodes, 'explicit', undefined);
      expect(r1.error).toContain('requires a selected leader node');

      const r2 = buildClusterPlan(nodes, 'explicit', '192.168.1.99');
      expect(r2.error).toContain('must be a member of the sync group');
    });

    it('fails if no nodes provided', () => {
      const r = buildClusterPlan([], 'auto');
      expect(r.error).toContain('No nodes selected');
    });
  });

  describe('evaluateClusterTopology', () => {
    it('returns NO_LEADER when 0 nodes have activeRole master', () => {
      const topo = evaluateClusterTopology([
        { ip: '192.168.1.10', status: { activeRole: 'worker', masterAddress: '192.168.1.5' } as any },
        { ip: '192.168.1.11', status: { activeRole: 'worker', masterAddress: '192.168.1.5' } as any },
      ]);
      expect(topo.state).toBe('NO_LEADER');
      expect(topo.leaderIp).toBeNull();
      expect(topo.isReady).toBe(false);
      expect(topo.error).toContain('NO LEADER');
    });

    it('returns VALID_LEADER when exactly 1 master and workers track that master', () => {
      const topo = evaluateClusterTopology([
        { ip: '192.168.1.10', status: { activeRole: 'master', masterAddress: null, transportApplyOk: true } as any },
        { ip: '192.168.1.11', status: { activeRole: 'worker', masterAddress: '192.168.1.10', transportApplyOk: true } as any },
      ]);
      expect(topo.state).toBe('VALID_LEADER');
      expect(topo.leaderIp).toBe('192.168.1.10');
      expect(topo.isReady).toBe(true);
      expect(topo.error).toBeUndefined();
    });

    it('returns SPLIT_BRAIN when multiple nodes report activeRole master', () => {
      const topo = evaluateClusterTopology([
        { ip: '192.168.1.10', status: { activeRole: 'master' } as any },
        { ip: '192.168.1.11', status: { activeRole: 'master' } as any },
      ]);
      expect(topo.state).toBe('SPLIT_BRAIN');
      expect(topo.leaderIp).toBeNull();
      expect(topo.isReady).toBe(false);
      expect(topo.error).toContain('SPLIT BRAIN (2 masters detected)');
    });

    it('returns LEADER_DISAGREEMENT when a worker tracks a different master', () => {
      const topo = evaluateClusterTopology([
        { ip: '192.168.1.10', status: { activeRole: 'master' } as any },
        { ip: '192.168.1.11', status: { activeRole: 'worker', masterAddress: '192.168.1.99' } as any },
      ]);
      expect(topo.state).toBe('LEADER_DISAGREEMENT');
      expect(topo.leaderIp).toBe('192.168.1.10');
      expect(topo.isReady).toBe(false);
      expect(topo.error).toContain('LEADER DISAGREEMENT');
    });

    it('marks isReady false when any follower reports transportApplyOk false', () => {
      const topo = evaluateClusterTopology([
        { ip: '192.168.1.10', status: { activeRole: 'master', transportApplyOk: true } as any },
        { ip: '192.168.1.11', status: { activeRole: 'worker', masterAddress: '192.168.1.10', transportApplyOk: false } as any },
      ]);
      expect(topo.state).toBe('VALID_LEADER');
      expect(topo.isReady).toBe(false);
      expect(topo.error).toContain('transport apply error');
    });
  });

  describe('Playback vs Schedule Parity', () => {
    const baseManifest: ShowManifest = {
      schema: 2,
      protocol: 4,
      playbackHash: 11111,
      scheduleHash: 22222,
      showHash: 33333,
      scenes: [{ index: 0, name: 'Brand', checksum: 100, motionChecksum: 200 }],
      timeline: { enabled: true, bpm: 120, checksum: 300 },
      cueLists: [{ index: 0, name: 'Main Cue', checksum: 400 }],
      schedules: { checksum: 500 },
    };

    it('comparePlaybackManifest returns MATCHED even if schedules differ', () => {
      const targetManifest: ShowManifest = {
        ...baseManifest,
        scheduleHash: 99999, // Schedule difference
        showHash: 88888,
        schedules: { checksum: 900 },
      };

      const res = comparePlaybackManifest(baseManifest, targetManifest);
      expect(res.status).toBe('MATCHED');
      expect(res.diffs.find((d) => d.category === 'scene')?.state).toBe('match');
      expect(res.diffs.find((d) => d.category === 'timeline')?.state).toBe('match');
      expect(res.diffs.find((d) => d.category === 'cueList')?.state).toBe('match');
      // Schedule diff should not even be in playback manifest diffs
      expect(res.diffs.find((d) => d.category === 'schedules')).toBeUndefined();
    });

    it('comparePlaybackManifest returns DRIFT if scenes or cue lists differ', () => {
      const targetManifest: ShowManifest = {
        ...baseManifest,
        playbackHash: 44444,
        scenes: [{ index: 0, name: 'Brand', checksum: 999, motionChecksum: 200 }],
      };

      const res = comparePlaybackManifest(baseManifest, targetManifest);
      expect(res.status).toBe('DRIFT');
      expect(res.diffs.find((d) => d.category === 'scene')?.state).toBe('changed');
    });

    it('compareScheduleManifest detects schedule diffs', () => {
      const targetManifest: ShowManifest = {
        ...baseManifest,
        schedules: { checksum: 999 },
      };

      const match = compareScheduleManifest(baseManifest, baseManifest);
      expect(match.status).toBe('MATCHED');

      const drift = compareScheduleManifest(baseManifest, targetManifest);
      expect(drift.status).toBe('DRIFT');
      expect(drift.diffs[0].state).toBe('changed');
    });
  });

  describe('generateSyncToken', () => {
    it('generates non-zero 32-bit integer when crypto available', () => {
      const token = generateSyncToken();
      expect(token).toBeGreaterThan(0);
      expect(token).toBeLessThanOrEqual(0xffffffff);
    });

    it('throws error when crypto is unavailable', () => {
      const origCrypto = globalThis.crypto;
      try {
        // @ts-ignore
        delete globalThis.crypto;
        expect(() => generateSyncToken()).toThrow('Secure random generation unavailable in this browser');
      } finally {
        globalThis.crypto = origCrypto;
      }
    });
  });
});
