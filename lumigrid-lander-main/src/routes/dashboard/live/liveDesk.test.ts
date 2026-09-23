import { describe, it, expect, vi, beforeEach } from 'vitest';
import { readFileSync } from 'node:fs';
import {
  expandChannelMask,
  channelsToMask,
  clampChannelValue,
  putManualState,
  releaseManualState,
  type ManualAddressableValue,
  type ManualPwmValue,
  type ManualChannelValue,
} from '$lib/api/manual';
import * as transport from '$lib/api/transport';

function readSource(relativePath: string): string {
  return readFileSync(new URL(relativePath, import.meta.url), 'utf8');
}

describe('Live Desk & Channel Group contracts', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  describe('mixed group targeting', () => {
    it('expands mixed group mask and generates segregated A and P commands', () => {
      // Group with A1 (0), A2 (1), and P1 (8)
      const mixedMask = (1 << 0) | (1 << 1) | (1 << 8); // 1 + 2 + 256 = 259
      const channels = expandChannelMask(mixedMask);
      expect(channels).toEqual([0, 1, 8]);

      // Split into addressable and PWM targets
      const aTargets = channels.filter(i => i < 8);
      const pTargets = channels.filter(i => i >= 8);

      expect(aTargets).toEqual([0, 1]);
      expect(pTargets).toEqual([8]);

      // Apply addressable color
      const aValues: ManualAddressableValue[] = aTargets.map(index => ({
        index,
        r: 255,
        g: 120,
        b: 50,
        brightness: 200,
      }));

      // Apply PWM fader
      const pValues: ManualPwmValue[] = pTargets.map(index => ({
        index,
        pwm: 3000,
      }));

      const combined: ManualChannelValue[] = [...aValues, ...pValues];
      const sanitized = combined.map(clampChannelValue);

      // Verify channel 0 (A1) has RGB and no PWM
      expect(sanitized[0]).toEqual({
        index: 0,
        r: 255,
        g: 120,
        b: 50,
        brightness: 200,
      });
      expect('pwm' in sanitized[0]).toBe(false);

      // Verify channel 8 (P1) has PWM and no RGB
      expect(sanitized[2]).toEqual({
        index: 8,
        pwm: 3000,
      });
      expect('r' in sanitized[2]).toBe(false);
      expect('brightness' in sanitized[2]).toBe(false);
    });
  });

  describe('no factory PWM leakage into normal live desk or channels', () => {
    it('ensures neither channels nor live desk imports or calls postFactoryPwm', () => {
      const channelsSource = readSource('../channels/+page.svelte');
      const liveDeskSource = readSource('./+page.svelte');

      const factoryPwmPattern = /postFactoryPwm|factory\/pwm/;

      expect(channelsSource).not.toMatch(factoryPwmPattern);
      expect(liveDeskSource).not.toMatch(factoryPwmPattern);
    });
  });

  describe('node switch teardown contract', () => {
    it('dispatches DELETE /api/v1/manual on session release', async () => {
      const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ released: true });

      const res = await releaseManualState('192.168.1.50');
      expect(res.released).toBe(true);

      expect(apiJsonSpy).toHaveBeenCalledWith(
        '192.168.1.50',
        '/api/v1/manual',
        expect.objectContaining({
          method: 'DELETE',
        }),
      );
    });
  });

  describe('role permission constraints', () => {
    it('ensures channel page enforces admin checks on configuration persistence', () => {
      const channelsSource = readSource('../channels/+page.svelte');

      // saveChannelConfig must check isAdmin
      expect(channelsSource).toMatch(/if\s*\(!isAdmin\)\s*\{\s*toastMsg\(/);

      // saveGroupConfig must check isAdmin
      expect(channelsSource).toMatch(/async\s+function\s+saveGroupConfig\(\)[\s\S]{0,120}if\s*\(!isAdmin\)/);

      // The save button is conditional on isAdmin
      expect(channelsSource).toMatch(/\{#if isAdmin\}[\s\S]{0,250}Save Channel/);
    });
  });

  describe('LG-024 Live Desk hardening contracts', () => {
    it('verifies +page.svelte delegates manual lifecycle to ManualSession and eliminates buildFullSnapshot', () => {
      const liveDeskSource = readSource('./+page.svelte');

      // Must import ManualSession
      expect(liveDeskSource).toMatch(/import\s*\{\s*ManualSession/);

      // Must NOT contain the old full-snapshot builder that caused channel leakage
      expect(liveDeskSource).not.toMatch(/function\s+buildFullSnapshot\s*\(/);

      // Must separate selectedIndices from controlledIndices
      expect(liveDeskSource).toMatch(/let\s+controlledIndices\s*=\s*\$state/);
      expect(liveDeskSource).toMatch(/let\s+selectedIndices\s*=\s*\$state/);

      // Must provide explicit Update Targets flow
      expect(liveDeskSource).toMatch(/async\s+function\s+updateTargets\s*\(/);
      expect(liveDeskSource).toMatch(/Update Targets/);

      // Must delegate telemetry to session
      expect(liveDeskSource).toMatch(/session\.onTelemetry\(/);

      // Must teardown cleanly via session.destroy
      expect(liveDeskSource).toMatch(/session\.destroy\(/);
    });
  });
});

