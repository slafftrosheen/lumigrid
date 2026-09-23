import { describe, it, expect, vi, beforeEach } from 'vitest';
import { deployShowToNodes, DEFAULT_SHOW_DEPLOYMENT_OPTIONS } from './showDeployment';
import * as syncApi from './sync';
import * as scenesApi from './scenes';
import * as sceneMotionApi from './sceneMotion';
import * as cuesApi from './cues';
import * as timelineApi from './timeline';
import * as nodeApi from './node';

describe('showDeployment service', () => {
  const sourceIp = '192.168.1.100';
  const targetIp1 = '192.168.1.101';
  const targetIp2 = '192.168.1.102';

  const mockSourceManifest: syncApi.ShowManifest = {
    schema: 1,
    protocol: 1,
    showHash: 55555,
    scenes: [
      { index: 0, name: 'Scene 0', checksum: 100, motionChecksum: 200 },
      { index: 1, name: 'Scene 1', checksum: 101, motionChecksum: 0 },
    ],
    timeline: { enabled: true, bpm: 120, checksum: 300 },
    schedules: { checksum: 400 },
    cueLists: [{ index: 0, name: 'Main Cue', checksum: 500 }],
  };

  const mockTargetPreManifest: syncApi.ShowManifest = {
    schema: 1,
    protocol: 1,
    showHash: 11111,
    scenes: [
      { index: 0, name: 'Scene 0 Old', checksum: 999, motionChecksum: 0 },
      { index: 5, name: 'Stale Scene', checksum: 888, motionChecksum: 0 }, // Extra scene
    ],
    timeline: { enabled: false, bpm: 100, checksum: 777 },
    schedules: { checksum: 666 },
    cueLists: [
      { index: 2, name: 'Stale Cue', checksum: 444 }, // Extra cue list
    ],
  };

  beforeEach(() => {
    vi.restoreAllMocks();

    vi.spyOn(syncApi, 'getShowManifest').mockImplementation(async (ip) => {
      if (ip === sourceIp) return mockSourceManifest;
      return mockTargetPreManifest;
    });

    vi.spyOn(scenesApi, 'getPreset').mockResolvedValue({
      index: 0,
      name: 'Scene 0',
      globalBrightness: 255,
      transitionMs: 500,
      zones: [] as any,
    });
    vi.spyOn(scenesApi, 'savePreset').mockResolvedValue({ saved: true });
    vi.spyOn(scenesApi, 'deletePreset').mockResolvedValue({ deleted: true });

    vi.spyOn(sceneMotionApi, 'getSceneMotion').mockResolvedValue({
      schema_version: 1,
      index: 0,
      transitionCurve: 'linear',
      trackCount: 1,
      tracks: [] as any,
    });
    vi.spyOn(sceneMotionApi, 'saveSceneMotion').mockResolvedValue({ saved: true });

    vi.spyOn(cuesApi, 'getCueList').mockResolvedValue({
      index: 0,
      name: 'Main Cue',
      mode: 'loop',
      endAction: 0,
      cues: [],
    });
    vi.spyOn(cuesApi, 'saveCueList').mockResolvedValue({ saved: true });
    vi.spyOn(cuesApi, 'deleteCueList').mockResolvedValue({ deleted: true });

    vi.spyOn(timelineApi, 'getSequence').mockResolvedValue({
      bpm: 120,
      enabled: true,
      tracks: [],
    });
    vi.spyOn(timelineApi, 'saveSequence').mockResolvedValue({ saved: true });

    vi.spyOn(nodeApi, 'getCalendarSchedule').mockResolvedValue({ entries: [] });
    vi.spyOn(nodeApi, 'saveCalendarSchedule').mockResolvedValue({ saved: true });
  });

  it('DEFAULT_SHOW_DEPLOYMENT_OPTIONS keeps schedules false and mirrorExactly false', () => {
    expect(DEFAULT_SHOW_DEPLOYMENT_OPTIONS.schedules).toBe(false);
    expect(DEFAULT_SHOW_DEPLOYMENT_OPTIONS.mirrorExactly).toBe(false);
    expect(DEFAULT_SHOW_DEPLOYMENT_OPTIONS.scenes).toBe(true);
    expect(DEFAULT_SHOW_DEPLOYMENT_OPTIONS.timeline).toBe(true);
  });

  it('deploys scenes, motions, cues, and timeline sequentially and verifies match', async () => {
    // When post-deploy manifest is fetched, simulate successful match
    vi.spyOn(syncApi, 'getShowManifest').mockImplementation(async (ip) => {
      if (ip === sourceIp) return mockSourceManifest;
      return {
        ...mockSourceManifest, // Post-deploy matches source
      };
    });

    const results = await deployShowToNodes(sourceIp, [targetIp1], { mirrorExactly: true });

    expect(results[targetIp1].state).toBe('complete');
    expect(results[targetIp1].error).toBeUndefined();

    // Verify correct calls were made
    expect(scenesApi.getPreset).toHaveBeenCalledWith(sourceIp, 0);
    expect(scenesApi.getPreset).toHaveBeenCalledWith(sourceIp, 1);
    expect(scenesApi.savePreset).toHaveBeenCalledWith(targetIp1, expect.objectContaining({ name: 'Scene 0' }));

    // Motion for scene 0 (which has motionChecksum > 0)
    expect(sceneMotionApi.getSceneMotion).toHaveBeenCalledWith(sourceIp, 0);
    expect(sceneMotionApi.saveSceneMotion).toHaveBeenCalledWith(targetIp1, 0, expect.anything());

    // Timeline
    expect(timelineApi.getSequence).toHaveBeenCalledWith(sourceIp);
    expect(timelineApi.saveSequence).toHaveBeenCalledWith(targetIp1, expect.anything());

    // Cue list
    expect(cuesApi.getCueList).toHaveBeenCalledWith(sourceIp, 0);
    expect(cuesApi.saveCueList).toHaveBeenCalledWith(targetIp1, 0, expect.anything());

    // Schedules should NOT have been called (default false)
    expect(nodeApi.getCalendarSchedule).not.toHaveBeenCalled();
    expect(nodeApi.saveCalendarSchedule).not.toHaveBeenCalled();
  });

  it('exact mirror deletes stale scenes and cue lists on target', async () => {
    // Pre-manifest has scene 5 and cue 2 which are not in source
    vi.spyOn(syncApi, 'getShowManifest').mockImplementation(async (ip) => {
      if (ip === sourceIp) return mockSourceManifest;
      return mockTargetPreManifest;
    });

    await deployShowToNodes(sourceIp, [targetIp1], { mirrorExactly: true });

    expect(scenesApi.deletePreset).toHaveBeenCalledWith(targetIp1, 5);
    expect(cuesApi.deleteCueList).toHaveBeenCalledWith(targetIp1, 2);
  });

  it('marks partial and records error if asset write fails', async () => {
    vi.spyOn(syncApi, 'getShowManifest').mockImplementation(async (ip) => {
      if (ip === sourceIp) return mockSourceManifest;
      return mockTargetPreManifest;
    });

    // Fail during saveSequence
    vi.spyOn(timelineApi, 'saveSequence').mockRejectedValue(new Error('Network timeout writing timeline'));

    const results = await deployShowToNodes(sourceIp, [targetIp1]);

    expect(results[targetIp1].state).toBe('partial');
    expect(results[targetIp1].error).toContain('Network timeout writing timeline');
  });

  it('deploys multiple targets with progress callback', async () => {
    vi.spyOn(syncApi, 'getShowManifest').mockImplementation(async (ip) => {
      if (ip === sourceIp) return mockSourceManifest;
      return { ...mockSourceManifest };
    });

    const progressReports: string[] = [];
    const results = await deployShowToNodes(
      sourceIp,
      [targetIp1, targetIp2],
      {},
      (update) => {
        progressReports.push(`${update[targetIp1]?.state}-${update[targetIp2]?.state}`);
      }
    );

    expect(results[targetIp1].state).toBe('complete');
    expect(results[targetIp2].state).toBe('complete');
    expect(progressReports.length).toBeGreaterThan(0);
  });
});
