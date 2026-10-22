import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createDefaultLayer,
  createDefaultZone,
  createDefaultPreset,
  clonePreset,
  serializePreset,
  isPresetDirty,
  computeZonePreviewColor,
  rgbToHex,
  hexToRgb,
  normalizeLayer,
  normalizeZone,
  normalizeScene,
  deletePreset,
  getPreset,
} from './scenes';
import * as scenesApi from './scenes';
import { EFFECT_METADATA, PALETTE_METADATA, LEGACY_EFFECTS, LEGACY_PALETTES } from './effectCatalog';

describe('scenes model & serialization', () => {
  it('creates valid default layers and zones', () => {
    const layer = createDefaultLayer();
    expect(layer.effect).toBe('solid');
    expect(layer.blendMode).toBe('normal');
    expect(layer.enabled).toBe(false);
    expect(layer.primaryColor).toEqual({ r: 255, g: 255, b: 255 });

    const zone = createDefaultZone();
    expect(zone.layers).toHaveLength(3);
    expect(zone.layers[0].enabled).toBe(true);
  });

  it('creates a default preset with 8 zones and clamped values', () => {
    const preset = createDefaultPreset(3, 'Test Scene');
    expect(preset.index).toBe(3);
    expect(preset.name).toBe('Test Scene');
    expect(preset.globalBrightness).toBe(128);
    expect(preset.zones).toHaveLength(8);
  });

  it('clones presets deeply without mutating original', () => {
    const original = createDefaultPreset(0, 'Original');
    const clone = clonePreset(original);
    clone.name = 'Modified';
    clone.zones[0].layers[0].enabled = false;

    expect(original.name).toBe('Original');
    expect(original.zones[0].layers[0].enabled).toBe(true);
    expect(clone.name).toBe('Modified');
    expect(clone.zones[0].layers[0].enabled).toBe(false);
  });

  it('serializes preset deterministically and detects dirty status', () => {
    const p1 = createDefaultPreset(1, 'Ambient');
    const p2 = clonePreset(p1);

    expect(isPresetDirty(p1, p2)).toBe(false);

    p2.globalBrightness = 200;
    expect(isPresetDirty(p2, p1)).toBe(true);

    const p3 = clonePreset(p1);
    p3.channels![2].layers[1].primaryColor.r = 120;
    expect(isPresetDirty(p3, p1)).toBe(true);
  });

  it('handles rgb <-> hex conversions accurately and rejects malformed values', () => {
    expect(rgbToHex({ r: 255, g: 0, b: 128 })).toBe('#ff0080');
    expect(hexToRgb('#ff0080')).toEqual({ r: 255, g: 0, b: 128 });
    expect(hexToRgb('#fff')).toEqual({ r: 255, g: 255, b: 255 });
    expect(hexToRgb('#FF00AA')).toEqual({ r: 255, g: 0, b: 170 });
    expect(hexToRgb('invalid')).toBeNull();
    expect(hexToRgb('')).toBeNull();
    expect(hexToRgb('#12345')).toBeNull();
    expect(hexToRgb('invalid', { r: 10, g: 20, b: 30 })).toEqual({ r: 10, g: 20, b: 30 });
  });

  it('normalizes malformed or partial scenes to valid 8-zone 3-layer structure', () => {
    const malformed = {
      index: 999, // out of range -> clamped to MAX_PRESETS - 1 (31)
      name: 'Overly long name that exceeds thirty-one chars allowed by spec',
      globalBrightness: -50, // clamped to 0
      transitionMs: 999999, // clamped to 10000
      zones: [
        {
          layers: [
            {
              effect: '  rainbow  ',
              blendMode: 'unknown_mode' as any,
              primaryColor: { r: 300, g: -10, b: NaN },
              speedMs: 1, // clamped to min 10
              fxParam: 500, // clamped to 255
              opacity: -20, // clamped to 0
              enabled: true,
            },
          ],
        },
      ],
    };

    const normalized = normalizeScene(malformed as any);
    expect(normalized.index).toBe(31);
    expect(normalized.name.length).toBeLessThanOrEqual(31);
    expect(normalized.globalBrightness).toBe(0);
    expect(normalized.transitionMs).toBe(10000);
    expect(normalized.zones).toHaveLength(8);
    for (const z of normalized.zones) {
      expect(z.layers).toHaveLength(3);
    }

    const layer0 = normalized.zones[0].layers[0];
    expect(layer0.effect).toBe('rainbow');
    expect(layer0.blendMode).toBe('normal');
    expect(layer0.primaryColor).toEqual({ r: 255, g: 0, b: 255 });
    expect(layer0.speedMs).toBe(10);
    expect(layer0.fxParam).toBe(255);
    expect(layer0.opacity).toBe(0);
    expect(layer0.enabled).toBe(true);

    // Serialization should be deterministic
    const s1 = serializePreset(normalized);
    const s2 = serializePreset(malformed as any);
    expect(s1).toBe(s2);
  });

  it('computes zone preview colors with layer blending and global brightness', () => {
    const zone = createDefaultZone();
    zone.layers[0].enabled = false;
    // With all layers disabled, preview is black
    const c0 = computeZonePreviewColor(zone, 255);
    expect(c0).toEqual({ r: 0, g: 0, b: 0 });

    // Enable layer 1 as solid red (normal blend)
    zone.layers[0].enabled = true;
    zone.layers[0].primaryColor = { r: 200, g: 0, b: 0 };
    zone.layers[0].opacity = 255;
    const c1 = computeZonePreviewColor(zone, 255);
    expect(c1).toEqual({ r: 200, g: 0, b: 0 });

    // Scale down with global brightness (50%)
    const c1Dim = computeZonePreviewColor(zone, 128);
    expect(c1Dim.r).toBeCloseTo(100, -1);

    // Additive blend with layer 2 as green
    zone.layers[1].enabled = true;
    zone.layers[1].blendMode = 'add';
    zone.layers[1].primaryColor = { r: 0, g: 150, b: 0 };
    zone.layers[1].opacity = 255;
    const c2 = computeZonePreviewColor(zone, 255);
    expect(c2).toEqual({ r: 200, g: 150, b: 0 });
  });

  it('issues DELETE request on deletePreset', async () => {
    const originalFetch = globalThis.fetch;
    const mockFetch = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ deleted: true, index: 5 }), { status: 200 })
    );
    vi.stubGlobal('fetch', mockFetch);
    try {
      const res = await deletePreset('192.168.1.50', 5);
      expect(mockFetch).toHaveBeenCalled();
      const [url, opts] = mockFetch.mock.calls[0];
      expect(url).toBe('http://192.168.1.50/api/v1/presets?index=5');
      expect(opts.method).toBe('DELETE');
      expect(res).toEqual({ deleted: true, index: 5 });
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('fetches single preset on getPreset', async () => {
    const originalFetch = globalThis.fetch;
    const mockScene = { index: 3, name: 'Lobby', globalBrightness: 255, transitionMs: 500, zones: [] };
    const mockFetch = vi.fn().mockResolvedValue(
      new Response(JSON.stringify(mockScene), { status: 200 })
    );
    vi.stubGlobal('fetch', mockFetch);
    try {
      const res = await getPreset('192.168.1.50', 3);
      expect(mockFetch).toHaveBeenCalled();
      const [url, opts] = mockFetch.mock.calls[0];
      expect(url).toBe('http://192.168.1.50/api/v1/presets?index=3');
      expect(opts?.method ?? 'GET').toBe('GET');
      expect(res.name).toBe('Lobby');
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('enforces MAX_SCENE_NAME_BYTES = 23 validation and UTF-8 truncation', () => {
    expect(scenesApi.MAX_SCENE_NAME_BYTES).toBe(23);
    expect(scenesApi.isValidSceneName('12345678901234567890123')).toBe(true);
    expect(scenesApi.isValidSceneName('123456789012345678901234')).toBe(false);
    expect(scenesApi.isValidSceneName('Hello World')).toBe(true);

    const truncated = scenesApi.truncateUtf8Bytes('12345678901234567890123456', 23);
    expect(scenesApi.getUtf8ByteLength(truncated)).toBeLessThanOrEqual(23);
    expect(truncated).toBe('12345678901234567890123');
  });

  it('detects dirty state on SceneDocument changes including motion tracks and curve', () => {
    const p1 = createDefaultPreset(2, 'Stage Warm');
    const m1 = {
      schema_version: 1,
      index: 2,
      transitionCurve: 'easeInOut' as const,
      trackCount: 1,
      tracks: [
        {
          target: 'globalBrightness' as const,
          channel: 0,
          layer: 0,
          curve: 'linear' as const,
          loop: true,
          periodMs: 3000,
          points: [{ position: 0, value: 0 }, { position: 65535, value: 65535 }],
        },
      ],
    };

    const doc1: scenesApi.SceneDocument = { scene: p1, motion: m1 };
    const doc2: scenesApi.SceneDocument = {
      scene: clonePreset(p1),
      motion: structuredClone(m1),
    };

    expect(scenesApi.isSceneDocumentDirty(doc1, doc2)).toBe(false);

    // Modify motion curve
    doc2.motion.transitionCurve = 'linear';
    expect(scenesApi.isSceneDocumentDirty(doc1, doc2)).toBe(true);
    doc2.motion.transitionCurve = 'easeInOut';

    // Modify automation track point
    doc2.motion.tracks[0].points[0].value = 100;
    expect(scenesApi.isSceneDocumentDirty(doc1, doc2)).toBe(true);
    doc2.motion.tracks[0].points[0].value = 0;

    // Modify scene layer
    doc2.scene.channels![0].layers[0].opacity = 180;
    expect(scenesApi.isSceneDocumentDirty(doc1, doc2)).toBe(true);
  });

  it('exports and validates imported scene document with strict schema', () => {
    const p = createDefaultPreset(4, 'Lobby Glow');
    const m = {
      schema_version: 1,
      index: 4,
      transitionCurve: 'easeOut' as const,
      trackCount: 0,
      tracks: [],
    };
    const doc: scenesApi.SceneDocument = { scene: p, motion: m };

    const exportedStr = scenesApi.exportSceneDocument(doc);
    expect(exportedStr).toContain('"type": "lumigrid-scene"');

    const importRes = scenesApi.validateImportedSceneDocument(exportedStr);
    expect(importRes.valid).toBe(true);
    if (importRes.valid) {
      expect(importRes.document.scene.name).toBe('Lobby Glow');
      expect(importRes.document.motion.transitionCurve).toBe('easeOut');
    }

    // Reject invalid type
    expect(scenesApi.validateImportedSceneDocument(JSON.stringify({ type: 'other' })).valid).toBe(false);

    // Reject > 4 tracks
    const tooManyTracks = {
      schema: 1,
      type: 'lumigrid-scene',
      scene: p,
      motion: {
        transitionCurve: 'linear',
        tracks: [{}, {}, {}, {}, {}], // 5 tracks
      },
    };
    expect(scenesApi.validateImportedSceneDocument(JSON.stringify(tooManyTracks)).valid).toBe(false);
  });

  it('saves combined SceneDocument with expected checksums', async () => {
    const originalFetch = globalThis.fetch;
    const mockFetch = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ saved: true, checksum: 1111, motionChecksum: 2222 }), { status: 200 })
    );
    vi.stubGlobal('fetch', mockFetch);
    try {
      const p = createDefaultPreset(5, 'Dual Save');
      const m = { schema_version: 1, index: 5, transitionCurve: 'linear' as const, trackCount: 0, tracks: [] };
      const doc: scenesApi.SceneDocument = { scene: p, motion: m, sceneChecksum: 123, motionChecksum: 456 };

      const res = await scenesApi.saveSceneDocument('192.168.1.50', doc);
      expect(mockFetch).toHaveBeenCalled();
      const [url, opts] = mockFetch.mock.calls[0];
      expect(url).toBe('http://192.168.1.50/api/v1/presets');
      expect(opts.method).toBe('PUT');
      const parsedBody = JSON.parse(opts.body);
      expect(parsedBody.channels).toHaveLength(16);
      expect(parsedBody.channels[0].layers).toHaveLength(3);
      expect(parsedBody.channels[0].layers[0].effect).toBe('solid');
      expect(parsedBody.expectedSceneChecksum).toBe(123);
      expect(parsedBody.expectedMotionChecksum).toBe(456);
      expect(parsedBody.motion).toBeDefined();
      expect(res.saved).toBe(true);
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('fetches palettes and scene references', async () => {
    const originalFetch = globalThis.fetch;
    const mockFetch = vi.fn().mockImplementation((url: string) => {
      if (url.includes('/api/v1/palettes')) {
        return Promise.resolve(new Response(JSON.stringify([{ id: 0, label: 'Custom' }, { id: 1, label: 'Rainbow' }])));
      }
      if (url.includes('/api/v1/presets/references')) {
        return Promise.resolve(new Response(JSON.stringify({ sceneIndex: 4, active: false, fallback: false, cues: [1], timelineTracks: [], schedules: [] })));
      }
      return Promise.reject(new Error('not found'));
    });
    vi.stubGlobal('fetch', mockFetch);
    try {
      const palettes = await scenesApi.getPalettes('192.168.1.50');
      expect(palettes).toHaveLength(2);
      expect(palettes[0].label).toBe('Custom');

      const refs = await scenesApi.getSceneReferences('192.168.1.50', 4);
      expect(refs.sceneIndex).toBe(4);
      expect(refs.cues).toEqual([1]);
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('prefers full channel values over the abbreviated legacy zones mirror', () => {
    const preset = createDefaultPreset(0);
    Object.assign(preset.channels![0].layers[0], { effect: 'confetti', paletteId: 13, fxParam: 15, speedMs: 4500 });
    const normalized = normalizeScene(preset);
    expect(normalized.zones[0].layers[0]).toEqual(normalized.channels![0].layers[0]);
    expect(normalized.zones[0].layers[0].paletteId).toBe(13);
  });

  it('round trips expanded effects, palettes and zero parameters through export and import', () => {
    const scene = createDefaultPreset(2, 'Expanded FX');
    Object.assign(scene.channels![0].layers[0], { effect: 'confetti', paletteId: 13, fxParam: 15 });
    Object.assign(scene.channels![1].layers[0], { effect: 'candle', fxParam: 0 });
    const document = { scene, motion: { index: 2, transitionCurve: 'linear' as const, tracks: [] } };
    const json = scenesApi.exportSceneDocument(document);
    const result = scenesApi.validateImportedSceneDocument(json, { effects: EFFECT_METADATA, palettes: PALETTE_METADATA });
    expect(result.valid).toBe(true);
    if (result.valid) {
      expect(result.document.scene.channels![0].layers[0].effect).toBe('confetti');
      expect(result.document.scene.channels![0].layers[0].paletteId).toBe(13);
      expect(result.document.scene.channels![1].layers[0].fxParam).toBe(0);
    }
    const olderNode = scenesApi.validateImportedSceneDocument(json, { effects: LEGACY_EFFECTS, palettes: LEGACY_PALETTES });
    expect(olderNode.valid).toBe(false);
    if (olderNode.valid === false) expect(olderNode.error).toContain('confetti');
  });

  it('validates palette membership against reported IDs, including sparse catalogues', () => {
    const scene = createDefaultPreset(0);
    scene.channels![0].layers[0].paletteId = 13;
    expect(scenesApi.validateSceneCatalog(scene, { effects: EFFECT_METADATA, palettes: [{ id: 0 }, { id: 13 }] }).valid).toBe(true);
    const result = scenesApi.validateSceneCatalog(scene, { effects: EFFECT_METADATA, palettes: LEGACY_PALETTES });
    expect(result.valid).toBe(false);
    if (result.valid === false) expect(result.error).toContain('palette 13');
  });

  it('saves new FX inside channel objects and refuses an unsupported catalogue before sending', async () => {
    const originalFetch = globalThis.fetch;
    const mockFetch = vi.fn().mockResolvedValue(new Response(JSON.stringify({ saved: true })));
    vi.stubGlobal('fetch', mockFetch);
    try {
      const scene = createDefaultPreset(0);
      Object.assign(scene.channels![0].layers[0], { effect: 'candle', paletteId: 13, fxParam: 0 });
      const doc = { scene, motion: { index: 0, transitionCurve: 'linear' as const, tracks: [] } };
      await scenesApi.saveSceneDocument('192.168.1.51', doc, { catalog: { effects: EFFECT_METADATA, palettes: PALETTE_METADATA } });
      const body = JSON.parse(mockFetch.mock.calls[0][1].body);
      expect(body.channels).toHaveLength(16);
      expect(body.zones).toBeUndefined();
      expect(body.channels[0].layers[0].effect).toBe('candle');
      expect(body.channels[0].layers[0].paletteId).toBe(13);
      expect(body.channels[0].layers[0].fxParam).toBe(0);
      mockFetch.mockClear();
      await expect(scenesApi.saveSceneDocument('192.168.1.51', doc, { catalog: { effects: LEGACY_EFFECTS, palettes: LEGACY_PALETTES } })).rejects.toThrow('not available');
      expect(mockFetch).not.toHaveBeenCalled();
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('refreshes catalogue caches after firmware changes and isolates node addresses', async () => {
    const originalFetch = globalThis.fetch;
    let effects = EFFECT_METADATA;
    let palettes = PALETTE_METADATA;
    const mockFetch = vi.fn().mockImplementation((url: string) => Promise.resolve(
      new Response(JSON.stringify(url.includes('/palettes') ? palettes : effects))
    ));
    vi.stubGlobal('fetch', mockFetch);
    try {
      expect(await scenesApi.getEffectsCached('192.168.1.60', 'cache-test', true)).toHaveLength(24);
      expect(await scenesApi.getPalettesCached('192.168.1.60', 'cache-test', true)).toHaveLength(14);
      effects = LEGACY_EFFECTS;
      palettes = LEGACY_PALETTES;
      expect(await scenesApi.getEffectsCached('192.168.1.60', 'cache-test', true)).toHaveLength(12);
      expect(await scenesApi.getPalettesCached('192.168.1.60', 'cache-test', true)).toHaveLength(6);
      expect(await scenesApi.getEffectsCached('192.168.1.61', 'cache-test')).toHaveLength(12);
      expect(mockFetch).toHaveBeenCalledTimes(5);
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('falls back for unavailable legacy endpoints, but propagates authentication failures', async () => {
    const originalFetch = globalThis.fetch;
    let status = 404;
    vi.stubGlobal('fetch', vi.fn().mockImplementation(() => Promise.resolve(new Response('{}', { status }))));
    try {
      expect(await scenesApi.getEffectsCached('192.168.1.62')).toHaveLength(12);
      expect(await scenesApi.getPalettesCached('192.168.1.62')).toHaveLength(6);
      status = 401;
      await expect(scenesApi.getEffectsCached('192.168.1.62')).rejects.toThrow('401');
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('saves PWM intensity through opacity without changing colors or unrelated channels', async () => {
    const originalFetch = globalThis.fetch;
    const mockFetch = vi.fn().mockResolvedValue(new Response(JSON.stringify({ saved: true })));
    vi.stubGlobal('fetch', mockFetch);
    try {
      const scene = createDefaultPreset(0, 'PWM levels');
      for (const [channel, effect] of [[8, 'candle'], [15, 'heartbeat']] as const) {
        Object.assign(scene.channels![channel].layers[0], {
          effect, opacity: 200, paletteId: 13, fxParam: 35, speedMs: 3456,
          primaryColor: { r: 16, g: 32, b: 64 },
          secondaryColor: { r: 120, g: 80, b: 40 },
        });
      }
      const before = clonePreset(scene);
      expect(scenesApi.getPwmIntensity(scene.channels![8].layers[0])).toBe(200);
      scenesApi.setPwmIntensity(scene.channels![8].layers[0], 0);
      scenesApi.setPwmIntensity(scene.channels![15].layers[0], 127.5);

      await scenesApi.saveSceneDocument('192.168.1.63', {
        scene, motion: { index: 0, transitionCurve: 'linear', tracks: [] },
      });
      const body = JSON.parse(mockFetch.mock.calls[0][1].body);
      expect(body.channels[8].layers[0]).toEqual({ ...before.channels![8].layers[0], opacity: 0 });
      expect(body.channels[15].layers[0]).toEqual({ ...before.channels![15].layers[0], opacity: 128 });
      expect(body.channels.slice(0, 8)).toEqual(before.channels!.slice(0, 8));
      expect(body.channels.slice(9, 15)).toEqual(before.channels!.slice(9, 15));
    } finally {
      vi.stubGlobal('fetch', originalFetch);
    }
  });

  it('keeps PWM intensity in the wire range and ignores nonfinite input', () => {
    const layer = createDefaultLayer();
    scenesApi.setPwmIntensity(layer, -1);
    expect(scenesApi.getPwmIntensity(layer)).toBe(0);
    scenesApi.setPwmIntensity(layer, 256);
    expect(scenesApi.getPwmIntensity(layer)).toBe(255);
    scenesApi.setPwmIntensity(layer, NaN);
    expect(scenesApi.getPwmIntensity(layer)).toBe(255);
  });
});
