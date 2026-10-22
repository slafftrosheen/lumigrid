import { describe, it, expect } from 'vitest';
import {
  EFFECT_METADATA, PALETTE_METADATA, normalizeEffectCatalog, normalizePaletteCatalog,
} from './effectCatalog';

describe('node-authoritative effect catalogue', () => {
  it('exposes all reported IDs without adding unsupported effects or palettes', () => {
    expect(normalizeEffectCatalog(EFFECT_METADATA)).toHaveLength(24);
    expect(normalizePaletteCatalog(PALETTE_METADATA)).toHaveLength(14);
    expect(normalizeEffectCatalog(EFFECT_METADATA.slice(0, 12))).toHaveLength(12);
    expect(normalizePaletteCatalog(PALETTE_METADATA.slice(0, 6))).toHaveLength(6);
    expect(normalizeEffectCatalog([])).toEqual([]);
    expect(normalizePaletteCatalog([])).toEqual([]);
  });

  it('uses conservative legacy IDs when the catalogue is unavailable', () => {
    expect(normalizeEffectCatalog(null)).toHaveLength(12);
    expect(normalizePaletteCatalog(undefined)).toHaveLength(6);
    expect(normalizeEffectCatalog(null).some(e => e.id === 'confetti')).toBe(false);
  });

  it('fills omitted metadata only for returned IDs and respects explicit support flags', () => {
    const effects = normalizeEffectCatalog([{ id: 'candle' }, { id: 'confetti', supportsPalette: false, parameterDefault: 30 }]);
    expect(effects).toHaveLength(2);
    expect(effects[0].supportsSecondaryColor).toBe(false);
    expect(effects[0].pwmRecommended).toBe(true);
    expect(effects[0].parameterDefault).toBe(35);
    expect(effects[1].supportsPalette).toBe(false);
    expect(effects[1].parameterDefault).toBe(30);
  });

  it('preserves zero bounds/defaults and uses device-defined metadata for future effects', () => {
    const [effect] = normalizeEffectCatalog([{
      id: 'future-effect', label: 'Future FX', hasParameter: true, supportsSpeed: true,
      supportsPalette: true, spatial: true, parameterMin: 0, parameterMax: 40, parameterDefault: 0,
    }]);
    expect(effect.id).toBe('future-effect');
    expect(effect.parameterMin).toBe(0);
    expect(effect.parameterDefault).toBe(0);
    expect(effect.pwmRecommended).toBe(false);
    expect(effect.supportsPalette).toBe(true);
  });

  it('keeps sparse and future palette IDs and omits invalid or duplicate entries', () => {
    expect(normalizePaletteCatalog([{ id: 13 }, { id: 40, label: 'Device palette' }, { id: 13 }, { id: -1 }, { id: 1.5 }]))
      .toEqual([{ id: 13, label: 'Twilight', usesSceneColors: false }, { id: 40, label: 'Device palette', usesSceneColors: false }]);
    expect(normalizeEffectCatalog([{ id: 'solid' }, { id: 'solid' }, null, { id: '' }])).toHaveLength(1);
  });
});
