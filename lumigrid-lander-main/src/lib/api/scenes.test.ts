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
  deletePreset,
} from './scenes';
import * as nodeApi from './node';

describe('scenes model & serialization', () => {
  it('creates valid default layers and zones', () => {
    const layer = createDefaultLayer();
    expect(layer.effect).toBe('solid');
    expect(layer.blendMode).toBe('normal');
    expect(layer.enabled).toBe(false);
    expect(layer.primaryColor).toEqual({ r: 255, g: 255, b: 255 });

    const zone = createDefaultZone();
    expect(zone.layers).toHaveLength(3);
    expect(zone.layers[0].enabled).toBe(false);
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
    clone.zones[0].layers[0].enabled = true;

    expect(original.name).toBe('Original');
    expect(original.zones[0].layers[0].enabled).toBe(false);
    expect(clone.name).toBe('Modified');
    expect(clone.zones[0].layers[0].enabled).toBe(true);
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

  it('handles rgb <-> hex conversions accurately', () => {
    expect(rgbToHex({ r: 255, g: 0, b: 128 })).toBe('#ff0080');
    expect(hexToRgb('#ff0080')).toEqual({ r: 255, g: 0, b: 128 });
    expect(hexToRgb('#fff')).toEqual({ r: 255, g: 255, b: 255 });
    expect(hexToRgb('invalid')).toEqual({ r: 255, g: 255, b: 255 });
  });

  it('computes zone preview colors with layer blending and global brightness', () => {
    const zone = createDefaultZone();
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

  it('calls savePreset with empty name on deletePreset', async () => {
    const spy = vi.spyOn(nodeApi, 'savePreset').mockResolvedValue({ saved: true });
    await deletePreset('192.168.1.50', 5);
    expect(spy).toHaveBeenCalledWith('192.168.1.50', { index: 5, name: '' });
    spy.mockRestore();
  });
});
