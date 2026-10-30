import type { EffectMeta, PaletteMeta } from './scenes';

// Local descriptions fill missing fields only. A successful node response owns
// the list of available IDs; never append this catalogue to a device response.
function effect(
  id: string, label: string, description: string,
  supportsSpeed: boolean, supportsSecondaryColor: boolean, supportsPalette: boolean,
  spatial: boolean, pwmRecommended: boolean,
  parameterLabel = '', parameterUnit = '', parameterMin = 0, parameterMax = 0, parameterDefault = 0,
): EffectMeta {
  return {
    id, label, description, supportsSpeed, supportsSecondaryColor, supportsPalette,
    spatial, pwmRecommended, parameterLabel, parameterUnit, parameterMin, parameterMax,
    parameterDefault, hasParameter: Boolean(parameterLabel),
  };
}

export const EFFECT_METADATA: readonly EffectMeta[] = [
  effect("solid", "Steady color", "A constant, even wash of color.", false, false, false, false, true),
  effect("breathe", "Smooth pulse", "Gently fades between a soft glow and full brightness.", true, true, true, false, true, "Minimum glow", "%", 1, 100, 4),
  effect("chase", "Moving comet", "A bright comet travels along the strip.", true, true, true, true, false, "Tail length", "pixels", 1, 8, 3),
  effect("sweep", "Color sweep", "Builds the primary color across the strip from end to end.", true, false, false, true, false),
  effect("reveal", "Soft reveal", "Fades the whole lane in for a polished sign-on.", true, false, false, false, true, "Fade length", "%", 10, 90, 25),
  effect("sparkle", "Random twinkle", "Adds crisp white twinkles over the base color every 50 ms.", false, false, false, true, false, "Twinkles", "per lane", 1, 12, 2),
  effect("gradient", "Flowing glow", "Moves a soft wave of light along the strip.", true, true, true, true, false, "Wave contrast", "%", 0, 100, 100),
  effect("strobe", "Beat strobe", "Flashes the lane with an adjustable on-time.", true, false, false, false, true, "Flash time", "%", 5, 95, 50),
  effect("fire", "Living flame", "Creates a flickering flame texture from the selected color.", true, true, true, true, false, "Flame height", "%", 20, 100, 70),
  effect("aurora", "Aurora glow", "Gently modulates the primary color across the whole lane.", true, false, false, false, true, "Color depth", "/255", 1, 255, 120),
  effect("colorwave", "Color wave", "Smooth sine wave color interpolation.", true, true, true, true, false, "Wave length", "px", 10, 255, 50),
  effect("staticpulse", "Static pulse", "Slow dimmer pulse for main/ambiance lights.", true, false, false, false, true, "Min dim", "%", 0, 100, 20),
  effect("rainbow", "Rainbow flow", "Moves a continuous full spectrum along the strip.", true, false, false, true, false, "Spectrum repeats", "", 1, 8, 1),
  effect("colorfade", "Color fade", "Blends the whole lane through colors, pausing at each end.", true, true, true, false, false, "Endpoint hold", "%", 0, 40, 10),
  effect("scanner", "Soft scanner", "A soft spotlight travels to the end of the strip and back.", true, true, true, true, false, "Spot width", "pixels", 1, 64, 6),
  effect("theater", "Marquee chase", "Spaced lights move smoothly along the strip.", true, true, true, true, false, "Spacing", "pixels", 2, 32, 6),
  effect("twinkle", "Soft twinkle", "Independent colored stars gently appear and fade over black.", true, true, true, true, false, "Density", "%", 1, 100, 25),
  effect("candle", "Candlelight", "An even wash flickers gently with an irregular dimmer rhythm.", true, false, false, false, true, "Flicker depth", "%", 0, 100, 35),
  effect("heartbeat", "Heartbeat", "Two soft pulses followed by a quiet rest.", true, false, false, false, true, "Minimum glow", "%", 0, 80, 4),
  effect("ripple", "Center ripple", "Soft rings expand from the center and fade away.", true, true, true, true, false, "Ring width", "pixels", 1, 64, 8),
  effect("dissolve", "Dissolve", "Pixels blend between colors in a repeatable shuffled order.", true, true, true, true, false, "Edge softness", "%", 1, 100, 15),
  effect("plasma", "Plasma flow", "Overlapping waves form a moving, colorful light texture.", true, true, true, true, false, "Scale", "waves", 1, 8, 3),
  effect("lightning", "Lightning", "A burst of flashes with varied strength, followed by a dark pause.", true, false, false, false, true, "Flashes", "per burst", 1, 5, 3),
  effect("confetti", "Confetti", "Scattered colored sparks appear and leave fading glows.", true, true, true, true, false, "Density", "%", 1, 100, 15),
];

export const PALETTE_METADATA: readonly PaletteMeta[] = [
  'Custom', 'Rainbow', 'Ocean', 'Lava', 'Forest', 'Sunset', 'Amber', 'Ice',
  'Neon', 'Lavender', 'Candy', 'Festive', 'Ember', 'Twilight',
].map((label, id) => ({ id, label, usesSceneColors: id === 0 }));

// The compatibility fallback deliberately contains only the pre-expansion IDs.
export const LEGACY_EFFECTS = EFFECT_METADATA.slice(0, 12);
export const LEGACY_PALETTES = PALETTE_METADATA.slice(0, 6);

export function normalizeEffectCatalog(data: unknown): EffectMeta[] {
  if (!Array.isArray(data)) return LEGACY_EFFECTS.map(e => ({ ...e }));
  const seen = new Set<string>();
  return data.flatMap((raw) => {
    if (!raw || typeof raw.id !== 'string' || !raw.id.trim() || seen.has(raw.id.trim())) return [];
    const id = raw.id.trim();
    seen.add(id);
    const known = EFFECT_METADATA.find(e => e.id === id);
    const meta: EffectMeta = {
      id, label: id, description: '', supportsSpeed: false,
      supportsSecondaryColor: false, supportsPalette: false,
      spatial: false, pwmRecommended: false, hasParameter: false,
      ...known, ...raw,
    };
    meta.id = id;
    meta.label = typeof raw.label === 'string' && raw.label.trim() ? raw.label : known?.label ?? id;
    for (const key of ['supportsSpeed', 'supportsSecondaryColor', 'supportsPalette', 'spatial', 'pwmRecommended', 'hasParameter'] as const) {
      meta[key] = typeof raw[key] === 'boolean' ? raw[key] : known?.[key] ?? false;
    }
    // Non-spatial does not imply a brightness envelope. Unknown device effects
    // need an explicit dimmer recommendation; never infer it from color motion.
    for (const key of ['description', 'parameterLabel', 'parameterUnit'] as const) {
      meta[key] = typeof raw[key] === 'string' ? raw[key] : known?.[key] ?? '';
    }
    const byte = (value: unknown, fallback: number) => typeof value === 'number' && Number.isFinite(value)
      ? Math.max(0, Math.min(255, Math.round(value))) : fallback;
    meta.parameterMin = byte(raw.parameterMin, known?.parameterMin ?? 0);
    meta.parameterMax = Math.max(meta.parameterMin, byte(raw.parameterMax, known?.parameterMax ?? 255));
    meta.parameterDefault = Math.max(meta.parameterMin, Math.min(meta.parameterMax, byte(raw.parameterDefault, known?.parameterDefault ?? meta.parameterMin)));
    return [meta];
  });
}

export function normalizePaletteCatalog(data: unknown): PaletteMeta[] {
  if (!Array.isArray(data)) return LEGACY_PALETTES.map(p => ({ ...p }));
  const seen = new Set<number>();
  return data.flatMap((raw) => {
    if (!raw || !Number.isInteger(raw.id) || raw.id < 0 || raw.id > 255 || seen.has(raw.id)) return [];
    seen.add(raw.id);
    const known = PALETTE_METADATA.find(p => p.id === raw.id);
    return [{
      id: raw.id,
      label: typeof raw.label === 'string' && raw.label.trim() ? raw.label : known?.label ?? `Palette ${raw.id}`,
      usesSceneColors: typeof raw.usesSceneColors === 'boolean' ? raw.usesSceneColors : raw.id === 0,
    }];
  });
}

export interface EffectGuidance {
  state: 'recommended' | 'limited' | 'unknown' | 'unavailable';
  message: string;
}

/** Presentation only: never changes a saved effect, scene, or output. */
export function effectGuidance(
  meta: EffectMeta | undefined, channelIndex: number, pixelCount?: number,
): EffectGuidance {
  if (!Number.isInteger(channelIndex) || channelIndex < 0 || channelIndex >= 16) {
    return { state: 'unknown', message: 'Choose an output to check this effect.' };
  }
  if (!meta) {
    return { state: 'unavailable', message: 'This saved effect is not reported by the connected node. It has not been replaced.' };
  }
  if (channelIndex >= 8) {
    if (meta.pwmRecommended === true) {
      return { state: 'recommended', message: 'Suitable for this dimmer: the effect controls brightness, not individual colors.' };
    }
    if (meta.pwmRecommended === false) {
      return { state: 'limited', message: 'This output controls brightness only. This effect is not recommended for a dimmer; strip color or movement may not appear.' };
    }
    return { state: 'unknown', message: 'Dimmer behavior is not declared for this effect. Check it at low brightness before use.' };
  }
  if (meta.spatial && pixelCount === 1) {
    return { state: 'limited', message: 'This effect moves along a strip. A single light point cannot show that movement.' };
  }
  return { state: 'recommended', message: 'This effect is available for the selected animated-strip output. Preview is illustrative; check the connected lights.' };
}
