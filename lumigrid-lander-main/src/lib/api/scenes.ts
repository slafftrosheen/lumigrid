import type { ScenePreset, SceneZone, SceneLayer, RgbColor } from './node';
import { savePreset } from './node';

export const ZONE_COUNT = 8;
export const LAYER_COUNT = 3;
export const MAX_PRESETS = 32;
export const BLEND_MODES = ['normal', 'add', 'multiply', 'screen'] as const;
export type BlendMode = typeof BLEND_MODES[number];

export function createDefaultLayer(): SceneLayer {
  return {
    effect: 'solid',
    blendMode: 'normal',
    primaryColor: { r: 255, g: 255, b: 255 },
    secondaryColor: { r: 0, g: 0, b: 0 },
    speedMs: 2000,
    fxParam: 0,
    paletteId: 0,
    opacity: 255,
    enabled: false,
  };
}

export function createDefaultZone(): SceneZone {
  return {
    layers: [createDefaultLayer(), createDefaultLayer(), createDefaultLayer()],
  };
}

export function createDefaultPreset(index: number, name?: string): ScenePreset {
  return {
    index,
    name: name ?? `Scene ${index}`,
    globalBrightness: 128,
    transitionMs: 1000,
    zones: Array.from({ length: ZONE_COUNT }, () => createDefaultZone()),
  };
}

export function clonePreset(preset: ScenePreset): ScenePreset {
  return structuredClone(preset);
}

export function serializePreset(preset: ScenePreset): string {
  return JSON.stringify({
    index: preset.index,
    name: preset.name.trim(),
    globalBrightness: Math.max(0, Math.min(255, Math.round(preset.globalBrightness))),
    transitionMs: Math.max(0, Math.round(preset.transitionMs)),
    zones: (preset.zones || []).map(zone => ({
      layers: (zone.layers || []).map(layer => ({
        effect: layer.effect,
        blendMode: layer.blendMode,
        primaryColor: {
          r: Math.max(0, Math.min(255, Math.round(layer.primaryColor.r))),
          g: Math.max(0, Math.min(255, Math.round(layer.primaryColor.g))),
          b: Math.max(0, Math.min(255, Math.round(layer.primaryColor.b))),
        },
        secondaryColor: {
          r: Math.max(0, Math.min(255, Math.round(layer.secondaryColor.r))),
          g: Math.max(0, Math.min(255, Math.round(layer.secondaryColor.g))),
          b: Math.max(0, Math.min(255, Math.round(layer.secondaryColor.b))),
        },
        speedMs: Math.max(10, Math.round(layer.speedMs)),
        fxParam: Math.max(0, Math.min(255, Math.round(layer.fxParam))),
        paletteId: Math.max(0, Math.round(layer.paletteId)),
        opacity: Math.max(0, Math.min(255, Math.round(layer.opacity))),
        enabled: Boolean(layer.enabled),
      })),
    })),
  });
}

export function isPresetDirty(current: ScenePreset | null, saved: ScenePreset | null): boolean {
  if (!current && !saved) return false;
  if (!current || !saved) return true;
  return serializePreset(current) !== serializePreset(saved);
}

export function rgbToHex(c: RgbColor): string {
  const clamp = (v: number) => Math.max(0, Math.min(255, Math.round(v)));
  const hex = ((1 << 24) + (clamp(c.r) << 16) + (clamp(c.g) << 8) + clamp(c.b)).toString(16).slice(1);
  return `#${hex}`;
}

export function hexToRgb(hex: string): RgbColor {
  const clean = hex.replace('#', '');
  const n = parseInt(clean.length === 3 ? clean.split('').map(x => x + x).join('') : clean, 16);
  if (isNaN(n)) return { r: 255, g: 255, b: 255 };
  return { r: (n >> 16) & 0xff, g: (n >> 8) & 0xff, b: n & 0xff };
}

export function computeZonePreviewColor(
  zone: SceneZone,
  globalBrightness = 255
): { r: number; g: number; b: number } {
  let r = 0;
  let g = 0;
  let b = 0;

  for (const layer of zone.layers) {
    if (!layer.enabled) continue;
    const a = Math.max(0, Math.min(255, layer.opacity)) / 255;
    const lr = layer.primaryColor.r;
    const lg = layer.primaryColor.g;
    const lb = layer.primaryColor.b;

    switch (layer.blendMode) {
      case 'add':
        r = Math.min(255, r + lr * a);
        g = Math.min(255, g + lg * a);
        b = Math.min(255, b + lb * a);
        break;
      case 'multiply': {
        const mr = (r * lr) / 255;
        const mg = (g * lg) / 255;
        const mb = (b * lb) / 255;
        r = mr * a + r * (1 - a);
        g = mg * a + g * (1 - a);
        b = mb * a + b * (1 - a);
        break;
      }
      case 'screen': {
        const sr = 255 - ((255 - r) * (255 - lr)) / 255;
        const sg = 255 - ((255 - g) * (255 - lg)) / 255;
        const sb = 255 - ((255 - b) * (255 - lb)) / 255;
        r = sr * a + r * (1 - a);
        g = sg * a + g * (1 - a);
        b = sb * a + b * (1 - a);
        break;
      }
      case 'normal':
      default:
        r = lr * a + r * (1 - a);
        g = lg * a + g * (1 - a);
        b = lb * a + b * (1 - a);
        break;
    }
  }

  const dim = Math.max(0, Math.min(255, globalBrightness)) / 255;
  return {
    r: Math.round(r * dim),
    g: Math.round(g * dim),
    b: Math.round(b * dim),
  };
}

export function deletePreset(ip: string, index: number): Promise<{ saved: true }> {
  return savePreset(ip, { index, name: '' });
}
