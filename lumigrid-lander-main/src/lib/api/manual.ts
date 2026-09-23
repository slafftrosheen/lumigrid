import { apiJson } from './transport';
import {
  createDefaultPreset,
  createDefaultLayer,
  CHANNEL_COUNT,
  ZONE_COUNT,
  type ScenePreset,
} from './scenes';

export interface ManualAddressableValue {
  index: number;
  r: number;
  g: number;
  b: number;
  brightness: number;
}

export interface ManualPwmValue {
  index: number;
  pwm: number;
}

export type ManualChannelValue = ManualAddressableValue | ManualPwmValue;

export interface ManualControlResponse {
  accepted: boolean;
  channelMask: number;
  leaseMs: number;
}

export interface ManualReleaseResponse {
  released: true;
}

export function clampByte(val: number): number {
  if (Number.isNaN(val) || !Number.isFinite(val)) return 0;
  return Math.max(0, Math.min(255, Math.round(val)));
}

export function clampPwm(val: number): number {
  if (Number.isNaN(val) || !Number.isFinite(val)) return 0;
  return Math.max(0, Math.min(4095, Math.round(val)));
}

/**
 * Ensures a channel value is clamped to valid ranges and matches its channel type:
 * - Indices 0-7: Addressable channel (r, g, b, brightness)
 * - Indices 8-15: PWM channel (pwm)
 */
export function clampChannelValue(ch: ManualChannelValue): ManualChannelValue {
  const index = Math.max(0, Math.min(15, Math.round(ch.index)));
  if (index >= 8) {
    const pwmVal = 'pwm' in ch ? ch.pwm : 0;
    return {
      index,
      pwm: clampPwm(pwmVal),
    };
  } else {
    const rVal = 'r' in ch ? ch.r : 0;
    const gVal = 'g' in ch ? ch.g : 0;
    const bVal = 'b' in ch ? ch.b : 0;
    const briVal = 'brightness' in ch ? ch.brightness : 255;
    return {
      index,
      r: clampByte(rVal),
      g: clampByte(gVal),
      b: clampByte(bVal),
      brightness: clampByte(briVal),
    };
  }
}

/**
 * Expands a 16-bit channel mask into an array of channel indices (0..15).
 */
export function expandChannelMask(mask: number): number[] {
  const indices: number[] = [];
  for (let i = 0; i < 16; i++) {
    if ((mask & (1 << i)) !== 0) {
      indices.push(i);
    }
  }
  return indices;
}

/**
 * Converts an array of channel indices (0..15) into a 16-bit channel mask.
 */
export function channelsToMask(indices: number[]): number {
  let mask = 0;
  for (const idx of indices) {
    if (idx >= 0 && idx < 16) {
      mask |= (1 << Math.round(idx));
    }
  }
  return mask;
}

/**
 * Sends a transient manual override snapshot replacing all active manual channels.
 * Leases are between 1,000 and 60,000 ms (default 15,000 ms).
 */
export async function putManualState(
  ip: string,
  channels: ManualChannelValue[],
  leaseMs = 15_000,
): Promise<ManualControlResponse> {
  const clampedLease = Math.max(1_000, Math.min(60_000, Math.round(leaseMs)));
  const sanitizedChannels = channels.map(clampChannelValue);

  return apiJson<ManualControlResponse>(ip, '/api/v1/manual', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      leaseMs: clampedLease,
      channels: sanitizedChannels,
    }),
  });
}

/**
 * Releases manual control immediately on the node.
 */
export async function releaseManualState(
  ip: string,
): Promise<ManualReleaseResponse> {
  return apiJson<ManualReleaseResponse>(ip, '/api/v1/manual', {
    method: 'DELETE',
  });
}

/**
 * Captures current Live Desk manual values into a new 16-channel Scene preset.
 * - Addressable channels: layer 0 solid, live RGB, opacity = brightness
 * - PWM channels: layer 0 solid, grayscale 0-255 derived from 12-bit PWM, opacity = 255
 * - Inactive channels: layer 0 disabled
 * - globalBrightness: 255
 */
export function captureLiveDeskToScene(
  presetIndex: number,
  sceneName: string,
  channels: ManualChannelValue[],
): ScenePreset {
  const preset = createDefaultPreset(presetIndex, sceneName);
  preset.globalBrightness = 255;
  preset.transitionMs = 1000;

  // Initialize all 16 channels to disabled layer 0
  const chs = Array.from({ length: CHANNEL_COUNT }, () => {
    const l0 = createDefaultLayer();
    l0.enabled = false;
    return {
      layers: [l0, createDefaultLayer(), createDefaultLayer()] as [typeof l0, typeof l0, typeof l0],
    };
  });
  preset.channels = chs;

  for (const raw of channels) {
    const ch = clampChannelValue(raw);
    if (ch.index < 0 || ch.index >= CHANNEL_COUNT) continue;
    const chan = preset.channels[ch.index];
    if (ch.index < 8 && 'r' in ch) {
      chan.layers[0].enabled = true;
      chan.layers[0].effect = 'solid';
      chan.layers[0].primaryColor = {
        r: ch.r,
        g: ch.g,
        b: ch.b,
      };
      chan.layers[0].opacity = ch.brightness;
    } else if (ch.index >= 8 && 'pwm' in ch) {
      chan.layers[0].enabled = true;
      chan.layers[0].effect = 'solid';
      const gray = Math.round((ch.pwm / 4095) * 255);
      chan.layers[0].primaryColor = { r: gray, g: gray, b: gray };
      chan.layers[0].opacity = 255;
    }
  }

  // Also populate zones 0..7 for backward compatibility with zone readers
  for (let z = 0; z < ZONE_COUNT; z++) {
    preset.zones[z] = preset.channels[z];
  }

  return preset;
}

