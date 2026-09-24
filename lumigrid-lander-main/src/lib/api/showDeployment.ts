import { getPreset, savePreset, deletePreset, deleteSceneMotion } from './scenes';
import { getSceneMotion, saveSceneMotion } from './sceneMotion';
import { getCueList, saveCueList, deleteCueList } from './cues';
import { getSequence, saveSequence } from './timeline';
import { getCalendarSchedule, saveCalendarSchedule } from './node';
import { getShowManifest, compareShowManifest, type ShowManifest } from './sync';

export interface ShowDeploymentOptions {
  scenes: boolean;
  sceneMotion: boolean;
  timeline: boolean;
  cueLists: boolean;
  schedules: boolean;
  mirrorExactly: boolean;
}

export const DEFAULT_SHOW_DEPLOYMENT_OPTIONS: ShowDeploymentOptions = {
  scenes: true,
  sceneMotion: true,
  timeline: true,
  cueLists: true,
  schedules: false, // Default false: schedules can be site/node specific
  mirrorExactly: false,
};

export interface ShowDeploymentNodeResult {
  ip: string;
  state: 'pending' | 'deploying' | 'verifying' | 'complete' | 'partial' | 'failed';
  currentAsset?: string;
  error?: string;
}

/**
 * Executes show deployment from a source node to one or more target nodes.
 *
 * Rules:
 * - Writes only selected categories.
 * - Schedules OFF by default.
 * - Target writes are sequential per target.
 * - Maximum 2 targets written concurrently.
 * - Failed asset produces PARTIAL state.
 * - Verification required before COMPLETE state.
 * - Exact mirror deletes stale Scenes and Cue Lists.
 * - Never calls /backup or /restore.
 */
export async function deployShowToNodes(
  sourceIp: string,
  targetIps: string[],
  userOptions: Partial<ShowDeploymentOptions> = {},
  onProgress?: (results: Record<string, ShowDeploymentNodeResult>) => void
): Promise<Record<string, ShowDeploymentNodeResult>> {
  const options: ShowDeploymentOptions = {
    ...DEFAULT_SHOW_DEPLOYMENT_OPTIONS,
    ...userOptions,
  };

  const results: Record<string, ShowDeploymentNodeResult> = {};
  for (const ip of targetIps) {
    results[ip] = { ip, state: 'pending' };
  }
  onProgress?.({ ...results });

  if (targetIps.length === 0) {
    return results;
  }

  // 1. Fetch source show manifest
  let sourceManifest: ShowManifest;
  try {
    sourceManifest = await getShowManifest(sourceIp);
  } catch (err: any) {
    const errorMsg = `Failed to fetch source manifest from ${sourceIp}: ${err?.message || err}`;
    for (const ip of targetIps) {
      results[ip] = { ip, state: 'failed', error: errorMsg };
    }
    onProgress?.({ ...results });
    return results;
  }

  // Concurrency worker queue (max 2 targets at a time)
  const concurrency = 2;
  const queue = [...targetIps];

  async function worker() {
    while (queue.length > 0) {
      const targetIp = queue.shift();
      if (!targetIp) break;

      await deploySingleTarget(sourceIp, targetIp, sourceManifest, options, (updated) => {
        results[targetIp] = updated;
        onProgress?.({ ...results });
      });
    }
  }

  const workers = Array.from({ length: Math.min(concurrency, targetIps.length) }, () => worker());
  await Promise.all(workers);

  return results;
}

async function deploySingleTarget(
  sourceIp: string,
  targetIp: string,
  sourceManifest: ShowManifest,
  options: ShowDeploymentOptions,
  updateState: (res: ShowDeploymentNodeResult) => void
): Promise<void> {
  updateState({ ip: targetIp, state: 'deploying', currentAsset: 'Pre-deploy manifest' });

  let targetPreManifest: ShowManifest | null = null;
  try {
    targetPreManifest = await getShowManifest(targetIp);
  } catch (err: any) {
    // If manifest fails to load, mark target failed
    updateState({
      ip: targetIp,
      state: 'failed',
      error: `Could not read target manifest: ${err?.message || err}`,
    });
    return;
  }

  let assetsWrittenCount = 0;

  try {
    // ── 1. Scenes ─────────────────────────────────────────────────────────────
    if (options.scenes) {
      const sourceSceneIndices = new Set(sourceManifest.scenes.map((s) => s.index));

      // Push all source scenes
      for (const sceneMeta of sourceManifest.scenes) {
        updateState({
          ip: targetIp,
          state: 'deploying',
          currentAsset: `Scene ${sceneMeta.index}${sceneMeta.name ? ` (${sceneMeta.name})` : ''}`,
        });
        const preset = await getPreset(sourceIp, sceneMeta.index);
        await savePreset(targetIp, preset);
        assetsWrittenCount++;
      }

      // Exact mirror: delete target scenes that are not present in source
      if (options.mirrorExactly && targetPreManifest?.scenes) {
        for (const tScene of targetPreManifest.scenes) {
          if (!sourceSceneIndices.has(tScene.index)) {
            updateState({
              ip: targetIp,
              state: 'deploying',
              currentAsset: `Delete Scene ${tScene.index}`,
            });
            await deletePreset(targetIp, tScene.index);
            assetsWrittenCount++;
          }
        }
      }
    }

    // ── 2. Scene Motion ───────────────────────────────────────────────────────
    if (options.sceneMotion) {
      // Find all scenes in source with motions
      const motionIndices: number[] = [];
      for (const s of sourceManifest.scenes) {
        if ((s.motionChecksum ?? 0) > 0) {
          motionIndices.push(s.index);
        }
      }

      for (const idx of motionIndices) {
        updateState({
          ip: targetIp,
          state: 'deploying',
          currentAsset: `Scene Motion ${idx}`,
        });
        const motion = await getSceneMotion(sourceIp, idx);
        await saveSceneMotion(targetIp, idx, motion);
        assetsWrittenCount++;
      }

      // Exact mirror: delete stale target motion records where source scene has no motion
      if (options.mirrorExactly && targetPreManifest?.scenes) {
        for (const tScene of targetPreManifest.scenes) {
          const sScene = sourceManifest.scenes.find((s) => s.index === tScene.index);
          const sMotionCs = sScene?.motionChecksum ?? 0;
          const tMotionCs = tScene.motionChecksum ?? 0;
          if (tMotionCs > 0 && sMotionCs === 0) {
            updateState({
              ip: targetIp,
              state: 'deploying',
              currentAsset: `Delete Scene Motion ${tScene.index}`,
            });
            await deleteSceneMotion(targetIp, tScene.index);
            assetsWrittenCount++;
          }
        }
      }
    }

    // ── 3. Cue Lists ──────────────────────────────────────────────────────────
    if (options.cueLists && sourceManifest.cueLists) {
      const sourceCueIndices = new Set(sourceManifest.cueLists.map((c) => c.index));

      for (const cueMeta of sourceManifest.cueLists) {
        updateState({
          ip: targetIp,
          state: 'deploying',
          currentAsset: `Cue List ${cueMeta.index}${cueMeta.name ? ` (${cueMeta.name})` : ''}`,
        });
        const cueList = await getCueList(sourceIp, cueMeta.index);
        await saveCueList(targetIp, cueMeta.index, cueList);
        assetsWrittenCount++;
      }

      // Exact mirror: delete extra cue lists
      if (options.mirrorExactly && targetPreManifest?.cueLists) {
        for (const tCue of targetPreManifest.cueLists) {
          if (!sourceCueIndices.has(tCue.index)) {
            updateState({
              ip: targetIp,
              state: 'deploying',
              currentAsset: `Delete Cue List ${tCue.index}`,
            });
            await deleteCueList(targetIp, tCue.index);
            assetsWrittenCount++;
          }
        }
      }
    }

    // ── 4. Timeline ───────────────────────────────────────────────────────────
    if (options.timeline) {
      updateState({ ip: targetIp, state: 'deploying', currentAsset: 'Timeline' });
      const sequence = await getSequence(sourceIp);
      await saveSequence(targetIp, sequence);
      assetsWrittenCount++;
    }

    // ── 5. Schedules ──────────────────────────────────────────────────────────
    if (options.schedules) {
      updateState({ ip: targetIp, state: 'deploying', currentAsset: 'Calendar Schedules' });
      const sched = await getCalendarSchedule(sourceIp);
      await saveCalendarSchedule(targetIp, sched);
      assetsWrittenCount++;
    }

    // ── 6. Verification ───────────────────────────────────────────────────────
    updateState({ ip: targetIp, state: 'verifying', currentAsset: 'Verifying show manifest' });
    const postManifest = await getShowManifest(targetIp);

    const verification = verifyShowDeployment(sourceManifest, postManifest, options);

    if (verification.complete) {
      updateState({ ip: targetIp, state: 'complete' });
    } else {
      updateState({
        ip: targetIp,
        state: 'partial',
        error: `Verification failed: ${verification.errors.join(', ')}`,
      });
    }
  } catch (err: any) {
    const errorMsg = err?.message || String(err);
    updateState({
      ip: targetIp,
      state: assetsWrittenCount > 0 ? 'partial' : 'failed',
      error: errorMsg,
    });
  }
}

export interface DeploymentVerification {
  complete: boolean;
  isComplete: boolean;
  categoryStatus: {
    scenes?: 'match' | 'mismatch' | 'ignored';
    sceneMotion?: 'match' | 'mismatch' | 'ignored';
    timeline?: 'match' | 'mismatch' | 'ignored';
    cueLists?: 'match' | 'mismatch' | 'ignored';
    schedules?: 'match' | 'mismatch' | 'ignored';
  };
  errors: string[];
}

/**
 * Structurally verifies show deployment against every selected category.
 *
 * Exact default Show deployment (Scenes, Motion, Timeline, CueLists = true; Schedules = false; MirrorExactly = true)
 * is considered COMPLETE when all four selected playback categories match even if schedules differ.
 */
export function verifyShowDeployment(
  source: ShowManifest,
  target: ShowManifest,
  options: Partial<ShowDeploymentOptions> = {}
): DeploymentVerification {
  const errors: string[] = [];
  const categoryStatus: DeploymentVerification['categoryStatus'] = {
    scenes: options.scenes ? 'match' : 'ignored',
    sceneMotion: options.sceneMotion ? 'match' : 'ignored',
    timeline: options.timeline ? 'match' : 'ignored',
    cueLists: options.cueLists ? 'match' : 'ignored',
    schedules: options.schedules ? 'match' : 'ignored',
  };

  // 1. Scenes
  if (options.scenes) {
    const postScenes = new Map(target.scenes.map((s) => [s.index, s]));
    for (const s of source.scenes) {
      const t = postScenes.get(s.index);
      if (!t) {
        errors.push(`Scene ${s.index} missing on target`);
        categoryStatus.scenes = 'mismatch';
      } else if (t.checksum !== s.checksum) {
        errors.push(`Scene ${s.index} checksum mismatch`);
        categoryStatus.scenes = 'mismatch';
      }
    }
    if (options.mirrorExactly) {
      const sourceIndices = new Set(source.scenes.map((s) => s.index));
      for (const t of target.scenes) {
        if (!sourceIndices.has(t.index)) {
          errors.push(`Extra stale target Scene ${t.index}`);
          categoryStatus.scenes = 'mismatch';
        }
      }
    }
  }

  // 2. Scene Motion
  if (options.sceneMotion) {
    const targetMotionMap = new Map(target.scenes.map((s) => [s.index, s.motionChecksum ?? 0]));
    for (const s of source.scenes) {
      const sMotionCs = s.motionChecksum ?? 0;
      if (sMotionCs > 0) {
        const tMotionCs = targetMotionMap.get(s.index) ?? 0;
        if (tMotionCs !== sMotionCs) {
          errors.push(`Scene Motion ${s.index} checksum mismatch`);
          categoryStatus.sceneMotion = 'mismatch';
        }
      }
    }
    if (options.mirrorExactly) {
      for (const t of target.scenes) {
        const s = source.scenes.find((sc) => sc.index === t.index);
        const sMotionCs = s?.motionChecksum ?? 0;
        const tMotionCs = t.motionChecksum ?? 0;
        if (tMotionCs > 0 && sMotionCs === 0) {
          errors.push(`Stale target Scene Motion ${t.index}`);
          categoryStatus.sceneMotion = 'mismatch';
        }
      }
    }
  }

  // 3. Cue Lists
  if (options.cueLists) {
    const targetCues = new Map((target.cueLists ?? []).map((c) => [c.index, c]));
    for (const s of source.cueLists ?? []) {
      const t = targetCues.get(s.index);
      if (!t) {
        errors.push(`Cue List ${s.index} missing on target`);
        categoryStatus.cueLists = 'mismatch';
      } else if (t.checksum !== s.checksum) {
        errors.push(`Cue List ${s.index} checksum mismatch`);
        categoryStatus.cueLists = 'mismatch';
      }
    }
    if (options.mirrorExactly) {
      const sourceCueIndices = new Set((source.cueLists ?? []).map((c) => c.index));
      for (const t of target.cueLists ?? []) {
        if (!sourceCueIndices.has(t.index)) {
          errors.push(`Extra stale target Cue List ${t.index}`);
          categoryStatus.cueLists = 'mismatch';
        }
      }
    }
  }

  // 4. Timeline
  if (options.timeline) {
    const sCs = source.timeline?.checksum ?? 0;
    const tCs = target.timeline?.checksum ?? 0;
    if (sCs !== tCs) {
      errors.push('Timeline sequence checksum mismatch');
      categoryStatus.timeline = 'mismatch';
    }
  }

  // 5. Schedules (only when options.schedules === true)
  if (options.schedules) {
    const sCs = source.schedules?.checksum ?? source.calendar?.checksum ?? 0;
    const tCs = target.schedules?.checksum ?? target.calendar?.checksum ?? 0;
    if (sCs !== tCs) {
      errors.push('Calendar schedule checksum mismatch');
      categoryStatus.schedules = 'mismatch';
    }
  }

  const isComplete = errors.length === 0;

  return {
    complete: isComplete,
    isComplete,
    categoryStatus,
    errors,
  };
}

export const verifyDeployment = verifyShowDeployment;

