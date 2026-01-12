import { derived, writable } from 'svelte/store';
import type { Writable } from 'svelte/store';

/**
 * @typedef {'rf' | 'neon' | 'sunset' | 'mint'} PaletteKey
 * Represents the available color palettes for the LED effects.
 */
export type PaletteKey = 'rf' | 'neon' | 'sunset' | 'mint';

/**
 * @interface EffectState
 * Defines the shape of the state object for controlling LED effects.
 * @property {PaletteKey} palette - The currently selected color palette.
 * @property {number} speed - The animation speed of the effect.
 * @property {number} hue - The base hue of the effect.
 * @property {number} saturation - The color saturation of the effect.
 * @property {number} brightness - The brightness of the effect.
 */
export interface EffectState {
  palette: PaletteKey;
  speed: number;
  hue: number;
  saturation: number;
  brightness: number;
}

/**
 * @type {Record<PaletteKey, string>}
 * A map of palette keys to their corresponding CSS repeating-linear-gradient values.
 */
export const palettes: Record<PaletteKey, string> = {
  rf:     'repeating-linear-gradient(90deg, rgba(28,197,220,.95) 0 6px, rgba(231,59,163,.95) 6px 12px, rgba(220,20,60,.95) 12px 18px, rgba(255,209,102,.95) 18px 24px)',
  neon:   'repeating-linear-gradient(90deg, rgba(0,229,255,.95) 0 6px, rgba(119,247,203,.95) 6px 12px, rgba(199,247,1,.95) 12px 18px, rgba(0,191,255,.95) 18px 24px)',
  sunset: 'repeating-linear-gradient(90deg, rgba(255,123,123,.95) 0 6px, rgba(255,162,76,.95) 6px 12px, rgba(255,209,102,.95) 12px 18px, rgba(255,214,231,.95) 18px 24px)',
  mint:   'repeating-linear-gradient(90deg, rgba(159,255,215,.95) 0 6px, rgba(118,231,255,.95) 6px 12px, rgba(202,166,255,.95) 12px 18px, rgba(217,255,247,.95) 18px 24px)'
};

/**
 * @type {Writable<EffectState>}
 * A writable Svelte store that holds the current state of the LED effects.
 * This allows components to subscribe to and update effect parameters.
 */
export const state: Writable<EffectState> = writable({
  palette: 'rf',
  speed: 1,
  hue: 355,
  saturation: 70,
  brightness: 90
});

/**
 * @type {import('svelte/store').Readable<string>}
 * A derived Svelte store that calculates the animation duration for the LED strip effect based on the speed in the state store.
 */
export const stripSpeed = derived(state, ($) => `${8 / $.speed}s`);

/**
 * @type {import('svelte/store').Readable<string>}
 * A derived Svelte store that calculates the CSS oklch() color value for the pulse effect based on the brightness, saturation, and hue in the state store.
 */
export const pulseColor = derived(state, ($) => `oklch(${$.brightness}% ${$.saturation / 100 * 0.15} ${$.hue})`);
