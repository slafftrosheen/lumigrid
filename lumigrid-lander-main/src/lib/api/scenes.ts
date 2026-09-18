import { apiJson } from './transport';

export interface RgbColor {
  r: number;
  g: number;
  b: number;
}

export interface SceneLayer {
  effect: string;
  blendMode: string;
  primaryColor: RgbColor;
  secondaryColor: RgbColor;
  speedMs: number;
  fxParam: number;
  paletteId: number;
  opacity: number;
  enabled: boolean;
}

export interface SceneZone {
  layers: [SceneLayer, SceneLayer, SceneLayer];
}

export interface ScenePreset {
  index: number;
  name: string;
  globalBrightness: number;
  transitionMs: number;
  zones: SceneZone[];
}

export interface EffectMeta {
  id: string;
  label: string;
  description: string;
  hasParameter: boolean;
  parameterLabel?: string;
  parameterUnit?: string;
  parameterMin?: number;
  parameterMax?: number;
  parameterDefault?: number;
}

export function getPresets(ip: string): Promise<ScenePreset[]> {
  return apiJson<ScenePreset[]>(ip, '/api/v1/presets');
}

export function savePreset(ip: string, preset: Partial<ScenePreset> & { index: number }): Promise<{ saved: true }> {
  return apiJson<{ saved: true }>(ip, '/api/v1/presets', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(preset),
  });
}

export interface ActivatePresetResult {
  accepted: true;
  preset: number;
  generation: number;
}

export interface SignageStatus {
  activePreset?: number;
  requestedPreset?: number;
  requestedGeneration?: number;
  appliedGeneration?: number;
  sceneApplyPending?: boolean;
  transitioning?: boolean;
  outputFault?: boolean;
  outputFaultCount?: number;
  controlOwner?: string;
}

export function activatePreset(
  ip: string,
  index: number,
): Promise<ActivatePresetResult> {
  return apiJson<ActivatePresetResult>(ip, '/api/v1/activate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ preset: index }),
  });
}

export function getSignageStatus(ip: string): Promise<SignageStatus> {
  return apiJson<SignageStatus>(ip, '/api/v1/status');
}

export function getEffects(ip: string): Promise<EffectMeta[]> {
  return apiJson<EffectMeta[]>(ip, '/api/v1/effects');
}

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
  const l0 = createDefaultLayer();
  l0.enabled = true;
  return {
    layers: [l0, createDefaultLayer(), createDefaultLayer()],
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

export function normalizeLayer(layer?: Partial<SceneLayer> | null): SceneLayer {
  const d = createDefaultLayer();
  if (!layer) return d;

  const clampU8 = (v: unknown, def: number) =>
    typeof v === 'number' && Number.isFinite(v) ? Math.max(0, Math.min(255, Math.round(v))) : def;
  const clampSpeed = (v: unknown, def: number) =>
    typeof v === 'number' && Number.isFinite(v) ? Math.max(10, Math.min(60000, Math.round(v))) : def;

  const primary: RgbColor = {
    r: clampU8(layer.primaryColor?.r, d.primaryColor.r),
    g: clampU8(layer.primaryColor?.g, d.primaryColor.g),
    b: clampU8(layer.primaryColor?.b, d.primaryColor.b),
  };

  const secondary: RgbColor = {
    r: clampU8(layer.secondaryColor?.r, d.secondaryColor.r),
    g: clampU8(layer.secondaryColor?.g, d.secondaryColor.g),
    b: clampU8(layer.secondaryColor?.b, d.secondaryColor.b),
  };

  const validBlend = typeof layer.blendMode === 'string' && (BLEND_MODES as readonly string[]).includes(layer.blendMode)
    ? layer.blendMode
    : 'normal';

  return {
    effect: typeof layer.effect === 'string' && layer.effect.trim() ? layer.effect.trim() : 'solid',
    blendMode: validBlend,
    primaryColor: primary,
    secondaryColor: secondary,
    speedMs: clampSpeed(layer.speedMs, d.speedMs),
    fxParam: clampU8(layer.fxParam, d.fxParam),
    paletteId: clampU8(layer.paletteId, d.paletteId),
    opacity: clampU8(layer.opacity, d.opacity),
    enabled: Boolean(layer.enabled),
  };
}

export function normalizeZone(zone?: Partial<SceneZone> | null): SceneZone {
  const rawLayers = (zone?.layers || []).slice(0, LAYER_COUNT);
  const layers: SceneLayer[] = [];
  for (let i = 0; i < LAYER_COUNT; i++) {
    layers.push(normalizeLayer(rawLayers[i]));
  }
  return {
    layers: [layers[0], layers[1], layers[2]],
  };
}

export function normalizeScene(preset: Partial<ScenePreset> | null, defaultIndex = 0): ScenePreset {
  const index = typeof preset?.index === 'number' && Number.isFinite(preset.index)
    ? Math.max(0, Math.min(MAX_PRESETS - 1, Math.round(preset.index)))
    : defaultIndex;
  const name = typeof preset?.name === 'string' ? preset.name.slice(0, 31).trim() : `Scene ${index}`;
  const globalBrightness = typeof preset?.globalBrightness === 'number' && Number.isFinite(preset.globalBrightness)
    ? Math.max(0, Math.min(255, Math.round(preset.globalBrightness)))
    : 128;
  const transitionMs = typeof preset?.transitionMs === 'number' && Number.isFinite(preset.transitionMs)
    ? Math.max(0, Math.min(10000, Math.round(preset.transitionMs)))
    : 1000;

  const rawZones = preset?.zones || [];
  const zones: SceneZone[] = [];
  for (let z = 0; z < ZONE_COUNT; z++) {
    zones.push(normalizeZone(rawZones[z]));
  }

  return {
    index,
    name,
    globalBrightness,
    transitionMs,
    zones,
  };
}

export function serializePreset(preset: ScenePreset): string {
  const norm = normalizeScene(preset, preset.index);
  return JSON.stringify(norm);
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

export function isValidHex(hex: unknown): boolean {
  if (typeof hex !== 'string') return false;
  return /^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.test(hex.trim());
}

export function hexToRgb(hex: string, fallback?: RgbColor): RgbColor | null {
  const trimmed = typeof hex === 'string' ? hex.trim() : '';
  if (!isValidHex(trimmed)) {
    return fallback ? { ...fallback } : null;
  }
  const clean = trimmed.replace('#', '');
  const expanded = clean.length === 3
    ? clean.split('').map(x => x + x).join('')
    : clean;
  const n = parseInt(expanded, 16);
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
