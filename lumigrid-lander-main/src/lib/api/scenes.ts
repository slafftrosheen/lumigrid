import { apiJson, ApiError } from './transport';
import { normalizeEffectCatalog, normalizePaletteCatalog } from './effectCatalog';
import type { SceneMotionConfig, AutomationTrackConfig } from './sceneMotion';

export const MAX_SCENE_NAME_BYTES = 23;
export const ZONE_COUNT = 8;
export const CHANNEL_COUNT = 16;
export const LAYER_COUNT = 3;
export const MAX_PRESETS = 32;
export const BLEND_MODES = ['normal', 'add', 'multiply', 'screen'] as const;
export type BlendMode = typeof BLEND_MODES[number];

export function getUtf8ByteLength(str: string): number {
  if (typeof str !== 'string') return 0;
  return new TextEncoder().encode(str).length;
}

export function isValidSceneName(name: string): boolean {
  if (typeof name !== 'string') return false;
  const len = getUtf8ByteLength(name.trim());
  return len >= 0 && len <= MAX_SCENE_NAME_BYTES;
}

export function truncateUtf8Bytes(str: string, maxBytes: number = MAX_SCENE_NAME_BYTES): string {
  if (typeof str !== 'string') return '';
  const encoder = new TextEncoder();
  const decoder = new TextDecoder('utf-8');
  const encoded = encoder.encode(str);
  if (encoded.length <= maxBytes) return str;
  return decoder.decode(encoded.slice(0, maxBytes)).replace(/\uFFFD$/, '');
}

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

// The renderer's PWM envelope is scaled by layer opacity, independently of RGB.
export function getPwmIntensity(layer: SceneLayer): number {
  return layer.opacity;
}

export function setPwmIntensity(layer: SceneLayer, value: number): void {
  if (!Number.isFinite(value)) return;
  layer.opacity = Math.max(0, Math.min(255, Math.round(value)));
}

export interface SceneZone {
  layers: [SceneLayer, SceneLayer, SceneLayer];
}

export type SceneChannel = SceneZone;

export interface ScenePreset {
  index: number;
  name: string;
  globalBrightness: number;
  transitionMs: number;
  channels?: SceneChannel[];
  zones: SceneZone[];
  checksum?: number;
}

export interface EffectMeta {
  id: string;
  label: string;
  description: string;
  supportsSpeed?: boolean;
  supportsSecondaryColor?: boolean;
  supportsPalette?: boolean;
  spatial?: boolean;
  pwmRecommended?: boolean;
  parameterLabel?: string;
  parameterUnit?: string;
  parameterMin?: number;
  parameterMax?: number;
  parameterDefault?: number;
  hasParameter?: boolean;
}

export interface PaletteMeta {
  id: number;
  label: string;
  usesSceneColors?: boolean;
}

export interface SceneCatalog {
  effects: readonly Pick<EffectMeta, 'id'>[];
  palettes: readonly Pick<PaletteMeta, 'id'>[];
}

export function validateSceneCatalog(
  scene: ScenePreset, catalog: SceneCatalog,
): { valid: true } | { valid: false; error: string } {
  const effects = new Set(catalog.effects.map(e => e.id));
  const palettes = new Set(catalog.palettes.map(p => p.id));
  const channels = scene.channels ?? scene.zones;
  for (let c = 0; c < channels.length; c++) {
    for (let l = 0; l < channels[c].layers.length; l++) {
      const layer = channels[c].layers[l];
      if (!effects.has(layer.effect)) {
        return { valid: false, error: `Channel ${c + 1}, layer ${l + 1}: effect "${layer.effect}" is not available on this node` };
      }
      if (!palettes.has(layer.paletteId)) {
        return { valid: false, error: `Channel ${c + 1}, layer ${l + 1}: palette ${layer.paletteId} is not available on this node` };
      }
    }
  }
  return { valid: true };
}

export interface SceneReferences {
  sceneIndex: number;
  active: boolean;
  fallback: boolean;
  cues: number[];
  timelineTracks: number[];
  schedules: number[];
}

export interface SceneDocument {
  scene: ScenePreset;
  motion: SceneMotionConfig;
  sceneChecksum?: number;
  motionChecksum?: number;
}

export function getPresets(ip: string): Promise<ScenePreset[]> {
  return apiJson<ScenePreset[]>(ip, '/api/v1/presets');
}

export function getPreset(ip: string, index: number): Promise<ScenePreset> {
  return apiJson<ScenePreset>(ip, `/api/v1/presets?index=${encodeURIComponent(index)}`);
}

export async function getPalettes(ip: string): Promise<PaletteMeta[]> {
  return normalizePaletteCatalog(await apiJson<unknown>(ip, '/api/v1/palettes'));
}

export function getSceneReferences(ip: string, index: number): Promise<SceneReferences> {
  return apiJson<SceneReferences>(ip, `/api/v1/presets/references?index=${encodeURIComponent(index)}`);
}

export async function getEffects(ip: string): Promise<EffectMeta[]> {
  return normalizeEffectCatalog(await apiJson<unknown>(ip, '/api/v1/effects'));
}

const effectsCache = new Map<string, EffectMeta[]>();
const palettesCache = new Map<string, PaletteMeta[]>();

export async function getEffectsCached(ip: string, cacheKey?: string, refresh = false): Promise<EffectMeta[]> {
  const key = cacheKey ? `${ip}:${cacheKey}` : undefined;
  if (key && !refresh && effectsCache.has(key)) {
    return effectsCache.get(key)!;
  }
  const data = await getEffects(ip).catch((error: unknown) => {
    if (error instanceof ApiError && (error.status === 404 || error.status === 501)) return normalizeEffectCatalog(null);
    throw error;
  });
  if (key) {
    effectsCache.set(key, data);
  }
  return data;
}

export async function getPalettesCached(ip: string, cacheKey?: string, refresh = false): Promise<PaletteMeta[]> {
  const key = cacheKey ? `${ip}:${cacheKey}` : undefined;
  if (key && !refresh && palettesCache.has(key)) {
    return palettesCache.get(key)!;
  }
  const data = await getPalettes(ip).catch((error: unknown) => {
    if (error instanceof ApiError && (error.status === 404 || error.status === 501)) return normalizePaletteCatalog(null);
    throw error;
  });
  if (key) {
    palettesCache.set(key, data);
  }
  return data;
}

export function savePreset(
  ip: string,
  preset: Partial<ScenePreset> & { index: number; motion?: unknown; expectedSceneChecksum?: number; expectedMotionChecksum?: number }
): Promise<{ saved: true; checksum?: number; motionChecksum?: number }> {
  return apiJson<{ saved: true; checksum?: number; motionChecksum?: number }>(ip, '/api/v1/presets', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(preset),
  });
}

export async function saveSceneDocument(
  ip: string,
  doc: SceneDocument,
  options?: { expectedSceneChecksum?: number; expectedMotionChecksum?: number; catalog?: SceneCatalog }
): Promise<{ saved: true; checksum?: number; motionChecksum?: number }> {
  const normScene = normalizeScene(doc.scene, doc.scene.index);
  if (options?.catalog) {
    const validation = validateSceneCatalog(normScene, options.catalog);
    if (validation.valid === false) throw new Error(validation.error);
  }
  const channelsPayload = normScene.channels!.map((ch) => ({
    layers: ch.layers.map((l) => ({
      effect: l.effect,
      blendMode: l.blendMode,
      primaryColor: l.primaryColor,
      secondaryColor: l.secondaryColor,
      speedMs: l.speedMs,
      fxParam: l.fxParam,
      paletteId: l.paletteId,
      opacity: l.opacity,
      enabled: l.enabled,
    })),
  }));

  const payload: Record<string, unknown> = {
    index: normScene.index,
    name: normScene.name,
    globalBrightness: normScene.globalBrightness,
    transitionMs: normScene.transitionMs,
    channels: channelsPayload,
    motion: {
      transitionCurve: doc.motion?.transitionCurve ?? 'easeInOut',
      tracks: doc.motion?.tracks ?? [],
    },
  };

  if (options?.expectedSceneChecksum !== undefined) {
    payload.expectedSceneChecksum = options.expectedSceneChecksum;
  } else if (doc.sceneChecksum !== undefined) {
    payload.expectedSceneChecksum = doc.sceneChecksum;
  }

  if (options?.expectedMotionChecksum !== undefined) {
    payload.expectedMotionChecksum = options.expectedMotionChecksum;
  } else if (doc.motionChecksum !== undefined) {
    payload.expectedMotionChecksum = doc.motionChecksum;
  }

  return apiJson<{ saved: true; checksum?: number; motionChecksum?: number }>(
    ip,
    '/api/v1/presets',
    {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    }
  );
}

export interface ActivatePresetResult {
  accepted: true;
  preset: number;
  generation: number;
  commandId?: number;
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

export function createDefaultChannel(): SceneChannel {
  return createDefaultZone();
}

export function createDefaultPreset(index: number, name?: string): ScenePreset {
  const rawName = name ?? `Scene ${index}`;
  const validName = truncateUtf8Bytes(rawName, MAX_SCENE_NAME_BYTES);
  return {
    index,
    name: validName,
    globalBrightness: 128,
    transitionMs: 1000,
    channels: Array.from({ length: CHANNEL_COUNT }, () => createDefaultZone()),
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
  let name = typeof preset?.name === 'string' ? preset.name.trim() : `Scene ${index}`;
  if (getUtf8ByteLength(name) > MAX_SCENE_NAME_BYTES) {
    name = truncateUtf8Bytes(name, MAX_SCENE_NAME_BYTES);
  }
  const globalBrightness = typeof preset?.globalBrightness === 'number' && Number.isFinite(preset.globalBrightness)
    ? Math.max(0, Math.min(255, Math.round(preset.globalBrightness)))
    : 128;
  const transitionMs = typeof preset?.transitionMs === 'number' && Number.isFinite(preset.transitionMs)
    ? Math.max(0, Math.min(10000, Math.round(preset.transitionMs)))
    : 1000;

  const rawZones = preset?.zones || [];
  const rawChannels = preset?.channels || [];
  const zones: SceneZone[] = [];
  for (let z = 0; z < ZONE_COUNT; z++) {
    zones.push(normalizeZone(rawChannels[z] || rawZones[z]));
  }

  const channels: SceneChannel[] = [];
  for (let c = 0; c < CHANNEL_COUNT; c++) {
    const fallback = c < ZONE_COUNT ? rawZones[c] : null;
    channels.push(normalizeZone(rawChannels[c] || fallback));
  }

  return {
    index,
    name,
    globalBrightness,
    transitionMs,
    channels,
    zones,
    checksum: typeof preset?.checksum === 'number' ? preset.checksum : undefined,
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

export function isSceneDocumentDirty(
  working: SceneDocument | null,
  persisted: SceneDocument | null
): boolean {
  if (!working && !persisted) return false;
  if (!working || !persisted) return true;
  if (isPresetDirty(working.scene, persisted.scene)) return true;

  const m1 = working.motion;
  const m2 = persisted.motion;
  if (!m1 && !m2) return false;
  if (!m1 || !m2) return true;
  if (m1.transitionCurve !== m2.transitionCurve) return true;

  const t1 = m1.tracks || [];
  const t2 = m2.tracks || [];
  if (t1.length !== t2.length) return true;

  for (let i = 0; i < t1.length; i++) {
    const a = t1[i];
    const b = t2[i];
    if (
      a.target !== b.target ||
      a.channel !== b.channel ||
      a.layer !== b.layer ||
      a.curve !== b.curve ||
      a.loop !== b.loop ||
      a.periodMs !== b.periodMs
    ) {
      return true;
    }
    const p1 = a.points || [];
    const p2 = b.points || [];
    if (p1.length !== p2.length) return true;
    for (let j = 0; j < p1.length; j++) {
      if (p1[j].position !== p2[j].position || p1[j].value !== p2[j].value) {
        return true;
      }
    }
  }

  return false;
}

export function exportSceneDocument(doc: SceneDocument): string {
  const normScene = normalizeScene(doc.scene, doc.scene.index);
  const data = {
    schema: 1,
    type: 'lumigrid-scene',
    scene: {
      index: normScene.index,
      name: normScene.name,
      globalBrightness: normScene.globalBrightness,
      transitionMs: normScene.transitionMs,
      channels: normScene.channels,
    },
    motion: {
      transitionCurve: doc.motion?.transitionCurve ?? 'easeInOut',
      tracks: doc.motion?.tracks ?? [],
    },
  };
  return JSON.stringify(data, null, 2);
}

export function validateImportedSceneDocument(
  rawInput: unknown,
  catalog?: SceneCatalog,
): { valid: true; document: SceneDocument } | { valid: false; error: string } {
  let data: any = rawInput;
  if (typeof rawInput === 'string') {
    try {
      data = JSON.parse(rawInput);
    } catch {
      return { valid: false, error: 'Invalid JSON format' };
    }
  }

  if (!data || typeof data !== 'object') {
    return { valid: false, error: 'Expected scene document object' };
  }

  if (data.type !== 'lumigrid-scene') {
    return { valid: false, error: 'Invalid file type: expected lumigrid-scene' };
  }

  if (data.schema !== 1) {
    return { valid: false, error: `Unsupported schema version: ${data.schema}` };
  }

  const s = data.scene;
  if (!s || typeof s !== 'object') {
    return { valid: false, error: 'Missing scene data in document' };
  }

  if (typeof s.name === 'string' && getUtf8ByteLength(s.name.trim()) > MAX_SCENE_NAME_BYTES) {
    return { valid: false, error: `Scene name exceeds ${MAX_SCENE_NAME_BYTES} UTF-8 bytes` };
  }

  if (s.channels && Array.isArray(s.channels) && s.channels.length > CHANNEL_COUNT) {
    return { valid: false, error: `Channels count exceeds maximum ${CHANNEL_COUNT}` };
  }

  if (s.channels && Array.isArray(s.channels)) {
    for (let c = 0; c < s.channels.length; c++) {
      const ch = s.channels[c];
      if (ch?.layers && Array.isArray(ch.layers) && ch.layers.length > LAYER_COUNT) {
        return { valid: false, error: `Channel ${c} has more than ${LAYER_COUNT} layers` };
      }
    }
  }

  const m = data.motion;
  if (m && typeof m === 'object') {
    if (m.tracks && Array.isArray(m.tracks)) {
      if (m.tracks.length > 4) {
        return { valid: false, error: 'Maximum 4 automation tracks allowed' };
      }
      for (let t = 0; t < m.tracks.length; t++) {
        const track = m.tracks[t];
        if (track?.points && Array.isArray(track.points)) {
          if (track.points.length > 6) {
            return { valid: false, error: `Track ${t} has more than 6 automation points` };
          }
          // Check sorting and duplicates
          for (let p = 1; p < track.points.length; p++) {
            if (track.points[p].position <= track.points[p - 1].position) {
              return { valid: false, error: `Track ${t} points must have strictly increasing positions` };
            }
          }
        }
      }
    }
  }

  const normalizedScene = normalizeScene(s);
  if (catalog) {
    const validation = validateSceneCatalog(normalizedScene, catalog);
    if (validation.valid === false) return validation;
  }
  const normalizedMotion: SceneMotionConfig = {
    schema_version: 1,
    index: normalizedScene.index,
    transitionCurve: m?.transitionCurve ?? 'easeInOut',
    trackCount: m?.tracks?.length ?? 0,
    tracks: Array.isArray(m?.tracks) ? m.tracks : [],
  };

  return {
    valid: true,
    document: {
      scene: normalizedScene,
      motion: normalizedMotion,
    },
  };
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

export function deletePreset(ip: string, index: number): Promise<{ deleted: boolean; index: number }> {
  return apiJson<{ deleted: boolean; index: number }>(ip, `/api/v1/presets?index=${encodeURIComponent(index)}`, {
    method: 'DELETE',
  });
}

export function deleteSceneMotion(ip: string, index: number): Promise<{ deleted: boolean; index: number }> {
  return apiJson<{ deleted: boolean; index: number }>(ip, `/api/v1/scene-motion?index=${encodeURIComponent(index)}`, {
    method: 'DELETE',
  });
}
