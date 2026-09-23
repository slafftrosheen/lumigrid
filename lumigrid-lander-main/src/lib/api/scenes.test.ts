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
    p3.zones[2].layers[1].primaryColor.r = 120;
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
});
