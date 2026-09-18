import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  createDefaultTrack,
  getSceneMotion,
  saveSceneMotion,
  type SceneMotionConfig,
} from './sceneMotion';

describe('scene motion channel contract', () => {
  const originalFetch = globalThis.fetch;

  beforeEach(() => {
    vi.restoreAllMocks();
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.stubGlobal('fetch', originalFetch);
  });

  it('normalizes legacy zone and target names to the 16-channel contract', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({
        schema_version: 1,
        index: 3,
        transitionCurve: 'easeInOut',
        tracks: [{
          target: 'speed',
          zone: 7,
          layer: 2,
          curve: 'linear',
          loop: true,
          periodMs: 2000,
          points: [{ position: 0, value: 1000 }, { position: 65535, value: 2000 }],
        }],
      }), { status: 200 }),
    );

    const motion = await getSceneMotion('192.168.1.50', 3);
    expect(motion.tracks).toHaveLength(1);
    expect(motion.tracks[0].target).toBe('layerSpeed');
    expect(motion.tracks[0].channel).toBe(7);
    expect(motion.tracks[0].layer).toBe(2);
  });

  it('preserves P8 as physical channel 15', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({
        index: 5,
        transitionCurve: 'easeInOut',
        tracks: [{
          target: 'layerParameter',
          channel: 15,
          layer: 1,
          curve: 'easeOut',
          loop: false,
          periodMs: 3500,
          points: [{ position: 0, value: 0 }, { position: 65535, value: 255 }],
        }],
      }), { status: 200 }),
    );

    const motion = await getSceneMotion('192.168.1.50', 5);
    expect(motion.tracks[0].channel).toBe(15);
    expect(motion.tracks[0].target).toBe('layerParameter');
  });

  it('saves canonical firmware target names and channel instead of zone', async () => {
    vi.mocked(fetch).mockResolvedValue(
      new Response(JSON.stringify({ saved: true, commandId: 9 }), { status: 200 }),
    );

    const track = createDefaultTrack('layerOpacity');
    track.channel = 12;
    track.layer = 2;

    const motion: SceneMotionConfig = {
      schema_version: 1,
      index: 6,
      transitionCurve: 'easeInOut',
      tracks: [track],
    };

    await saveSceneMotion('192.168.1.50', 6, motion);

    const [, options] = vi.mocked(fetch).mock.calls[0];
    const body = JSON.parse(String(options?.body));

    expect(body.trackCount).toBe(1);
    expect(body.tracks[0].target).toBe('layerOpacity');
    expect(body.tracks[0].channel).toBe(12);
    expect(body.tracks[0]).not.toHaveProperty('zone');
  });
});
