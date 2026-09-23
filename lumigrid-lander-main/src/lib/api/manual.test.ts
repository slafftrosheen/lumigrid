import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  clampByte,
  clampPwm,
  clampChannelValue,
  expandChannelMask,
  channelsToMask,
  putManualState,
  releaseManualState,
  captureLiveDeskToScene,
  type ManualAddressableValue,
  type ManualPwmValue,
  type ManualChannelValue,
} from './manual';
import * as transport from './transport';

describe('manual API', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  describe('clamping and sanitization', () => {
    it('clamps byte values to 0..255', () => {
      expect(clampByte(-10)).toBe(0);
      expect(clampByte(0)).toBe(0);
      expect(clampByte(128.4)).toBe(128);
      expect(clampByte(255)).toBe(255);
      expect(clampByte(300)).toBe(255);
      expect(clampByte(NaN)).toBe(0);
    });

    it('clamps pwm values to 0..4095', () => {
      expect(clampPwm(-50)).toBe(0);
      expect(clampPwm(0)).toBe(0);
      expect(clampPwm(2048.2)).toBe(2048);
      expect(clampPwm(4095)).toBe(4095);
      expect(clampPwm(5000)).toBe(4095);
      expect(clampPwm(NaN)).toBe(0);
    });

    it('clamps invalid RGB and brightness values for addressable channels (A1..A8)', () => {
      const input: ManualAddressableValue = {
        index: 0,
        r: -20,
        g: 300,
        b: 120,
        brightness: 999,
      };
      const clamped = clampChannelValue(input);
      expect(clamped).toEqual({
        index: 0,
        r: 0,
        g: 255,
        b: 120,
        brightness: 255,
      });
      // Ensure pwm field is not present
      expect('pwm' in clamped).toBe(false);
    });

    it('clamps invalid PWM values for PWM channels (P1..P8)', () => {
      const input: ManualPwmValue = {
        index: 8,
        pwm: 8000,
      };
      const clamped = clampChannelValue(input);
      expect(clamped).toEqual({
        index: 8,
        pwm: 4095,
      });
      // Ensure RGB fields are not present
      expect('r' in clamped).toBe(false);
      expect('brightness' in clamped).toBe(false);
    });

    it('enforces channel type boundary based on index', () => {
      // Index 2 treated as addressable even if given a pwm field
      const wrongA = clampChannelValue({ index: 2, pwm: 2000 } as unknown as ManualChannelValue);
      expect(wrongA).toEqual({
        index: 2,
        r: 0,
        g: 0,
        b: 0,
        brightness: 255,
      });

      // Index 10 treated as PWM even if given RGB fields
      const wrongP = clampChannelValue({
        index: 10,
        r: 255,
        g: 100,
        b: 50,
        brightness: 200,
      } as unknown as ManualChannelValue);
      expect(wrongP).toEqual({
        index: 10,
        pwm: 0,
      });
    });
  });

  describe('mask conversions', () => {
    it('expands channel mask to channel indices', () => {
      // 0x01 = channel 0 (A1)
      // 0x0100 = channel 8 (P1)
      // 0x0101 = 257 = A1 + P1
      expect(expandChannelMask(0)).toEqual([]);
      expect(expandChannelMask(257)).toEqual([0, 8]);
      expect(expandChannelMask(0x00FF)).toEqual([0, 1, 2, 3, 4, 5, 6, 7]);
      expect(expandChannelMask(0xFF00)).toEqual([8, 9, 10, 11, 12, 13, 14, 15]);
      expect(expandChannelMask(0xFFFF)).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
    });

    it('converts channel indices to 16-bit mask', () => {
      expect(channelsToMask([])).toBe(0);
      expect(channelsToMask([0, 8])).toBe(257);
      expect(channelsToMask([0, 1, 2, 3, 4, 5, 6, 7])).toBe(0x00FF);
      expect(channelsToMask([8, 9, 10, 11, 12, 13, 14, 15])).toBe(0xFF00);
      expect(channelsToMask([0, 15])).toBe(1 | (1 << 15));
    });
  });

  describe('HTTP calls', () => {
    it('sends PUT /api/v1/manual with full 16-channel state and clamped lease', async () => {
      const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
        accepted: true,
        channelMask: 0xFFFF,
        leaseMs: 15000,
      });

      const all16: (ManualAddressableValue | ManualPwmValue)[] = [];
      for (let i = 0; i < 8; i++) {
        all16.push({ index: i, r: 255, g: 0, b: 128, brightness: 200 });
      }
      for (let i = 8; i < 16; i++) {
        all16.push({ index: i, pwm: 2048 });
      }

      const res = await putManualState('192.168.1.100', all16, 15000);
      expect(res).toEqual({
        accepted: true,
        channelMask: 0xFFFF,
        leaseMs: 15000,
      });

      expect(apiJsonSpy).toHaveBeenCalledWith(
        '192.168.1.100',
        '/api/v1/manual',
        expect.objectContaining({
          method: 'PUT',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            leaseMs: 15000,
            channels: all16,
          }),
        }),
      );
    });

    it('clamps leaseMs within 1,000 and 60,000 ms', async () => {
      const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
        accepted: true,
        channelMask: 1,
        leaseMs: 1000,
      });

      await putManualState('192.168.1.100', [{ index: 0, r: 100, g: 100, b: 100, brightness: 255 }], 500);
      expect(apiJsonSpy).toHaveBeenCalledWith(
        '192.168.1.100',
        '/api/v1/manual',
        expect.objectContaining({
          body: JSON.stringify({
            leaseMs: 1000,
            channels: [{ index: 0, r: 100, g: 100, b: 100, brightness: 255 }],
          }),
        }),
      );
    });

    it('sends DELETE /api/v1/manual on release', async () => {
      const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
        released: true,
      });

      const res = await releaseManualState('192.168.1.100');
      expect(res).toEqual({ released: true });
      expect(apiJsonSpy).toHaveBeenCalledWith('192.168.1.100', '/api/v1/manual', {
        method: 'DELETE',
      });
    });
  });

  describe('captureLiveDeskToScene', () => {
    it('captures live desk values into a valid 16-channel ScenePreset', () => {
      const manualValues: (ManualAddressableValue | ManualPwmValue)[] = [
        { index: 0, r: 255, g: 64, b: 0, brightness: 220 },
        { index: 8, pwm: 2048 },
      ];

      const scene = captureLiveDeskToScene(3, 'Captured Live Look', manualValues);

      expect(scene.index).toBe(3);
      expect(scene.name).toBe('Captured Live Look');
      expect(scene.globalBrightness).toBe(255);
      expect(scene.transitionMs).toBe(1000);
      expect(scene.channels).toBeDefined();
      expect(scene.channels).toHaveLength(16);

      // Channel 0 (A1): active, solid RGB, opacity = 220
      const ch0 = scene.channels![0];
      expect(ch0.layers[0].enabled).toBe(true);
      expect(ch0.layers[0].effect).toBe('solid');
      expect(ch0.layers[0].primaryColor).toEqual({ r: 255, g: 64, b: 0 });
      expect(ch0.layers[0].opacity).toBe(220);

      // Channel 8 (P1): active, solid grayscale (2048/4095*255 ≈ 128), opacity = 255
      const ch8 = scene.channels![8];
      expect(ch8.layers[0].enabled).toBe(true);
      expect(ch8.layers[0].effect).toBe('solid');
      expect(ch8.layers[0].primaryColor).toEqual({ r: 128, g: 128, b: 128 });
      expect(ch8.layers[0].opacity).toBe(255);

      // Inactive channel (e.g. channel 1 A2): layer 0 disabled
      const ch1 = scene.channels![1];
      expect(ch1.layers[0].enabled).toBe(false);

      // Inactive channel (e.g. channel 9 P2): layer 0 disabled
      const ch9 = scene.channels![9];
      expect(ch9.layers[0].enabled).toBe(false);

      // Check backward compatibility zones
      expect(scene.zones).toHaveLength(8);
      expect(scene.zones[0].layers[0].enabled).toBe(true);
      expect(scene.zones[1].layers[0].enabled).toBe(false);
    });
  });
});

