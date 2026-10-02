import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  normalizeDeviceConfig,
  serializeDeviceConfig,
  getDeviceConfig,
  saveDeviceConfig,
  validateDeviceConfig,
  ConfigConflictError,
  type DeviceConfigWire,
  type DeviceConfig,
} from './config';
import * as transport from './transport';

describe('config client contract (LG-036.1)', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  const sampleLimits = {
    maxPowerMw: 90000,
    maxPixelsPerChannel: 1024,
    maxGroups: 8,
    maxChannels: 16,
    maxDmxUniverse: 64,
  };

  const sampleBaseConfig: DeviceConfig = {
    schema: 2,
    checksum: 0x1234ABCD,
    limits: sampleLimits,
    syncTokenSet: true,
    deviceName: 'lumigrid-node-01',
    activePresetIndex: 0,
    fallbackPresetIndex: 0,
    watchdogTimeoutS: 60,
    maxPowerMw: 45000,
    gammaX10: 22,
    softStartEnabled: true,
    softStartMs: 500,
    mqttEnabled: false,
    mqttBroker: '',
    mqttPort: 1883,
    syncEnabled: true,
    syncRole: 1,
    protocol: {
      artnetEnabled: false,
      sacnEnabled: false,
      universeStart: 1,
      liveStreamLayer: 2,
    },
  };

  describe('A & B & C: Normalization invariants', () => {
    it('preserves checksum and schema across normalize', () => {
      const wire: DeviceConfigWire = {
        schema: 2,
        checksum: 0xDEADBEEF,
        deviceName: 'lumigrid-01',
        limits: sampleLimits,
        syncTokenSet: true,
      };

      const normalized = normalizeDeviceConfig(wire);
      expect(normalized.schema).toBe(2);
      expect(normalized.checksum).toBe(0xDEADBEEF);
    });

    it('preserves authoritative limits from firmware', () => {
      const wire: DeviceConfigWire = {
        deviceName: 'lumigrid-01',
        checksum: 1234,
        limits: {
          maxPowerMw: 120000,
          maxPixelsPerChannel: 512,
          maxGroups: 8,
          maxChannels: 16,
          maxDmxUniverse: 32,
        },
      };

      const normalized = normalizeDeviceConfig(wire);
      expect(normalized.limits.maxPowerMw).toBe(120000);
      expect(normalized.limits.maxPixelsPerChannel).toBe(512);
      expect(normalized.limits.maxDmxUniverse).toBe(32);
    });

    it('syncToken is not expected from GET (syncTokenSet is boolean)', () => {
      const wire: DeviceConfigWire = {
        deviceName: 'lumigrid-01',
        checksum: 5678,
        syncTokenSet: true,
      };

      const normalized = normalizeDeviceConfig(wire);
      expect(normalized.syncTokenSet).toBe(true);
      // Secret is write-only and never read back
      expect((normalized as any).syncToken).toBeUndefined();
    });
  });

  describe('Serialization & Write-only syncToken', () => {
    it('serializeDeviceConfig does NOT include checksum, limits, or syncTokenSet', () => {
      const serialized = serializeDeviceConfig(sampleBaseConfig);
      expect((serialized as any).checksum).toBeUndefined();
      expect((serialized as any).limits).toBeUndefined();
      expect((serialized as any).syncTokenSet).toBeUndefined();
      expect((serialized as any).schema).toBeUndefined();
    });

    it('omits syncToken when no new token is supplied (preserves existing)', () => {
      const cfg: DeviceConfig = {
        ...sampleBaseConfig,
        pendingSyncToken: undefined,
        clearSyncToken: false,
      };
      const serialized = serializeDeviceConfig(cfg);
      expect(serialized.syncToken).toBeUndefined();
      expect(serialized.clearSyncToken).toBeUndefined();
    });

    it('includes syncToken when pendingSyncToken is supplied', () => {
      const cfg: DeviceConfig = {
        ...sampleBaseConfig,
        pendingSyncToken: 987654321,
      };
      const serialized = serializeDeviceConfig(cfg);
      expect(serialized.syncToken).toBe(987654321);
    });

    it('includes clearSyncToken when explicitly set to true', () => {
      const cfg: DeviceConfig = {
        ...sampleBaseConfig,
        clearSyncToken: true,
      };
      const serialized = serializeDeviceConfig(cfg);
      expect(serialized.clearSyncToken).toBe(true);
      expect(serialized.syncToken).toBeUndefined();
    });
  });

  describe('D & E: Config Save & Concurrency', () => {
    it('config save sends expectedChecksum in request body', async () => {
      let sentBody: any = null;
      vi.spyOn(transport, 'apiFetch').mockImplementation(async (_ip, _path, init) => {
        sentBody = JSON.parse(init?.body as string);
        return new Response(
          JSON.stringify({
            accepted: true,
            saved: true,
            commandId: 101,
            checksum: 0x9999,
            apply: 'ok',
          }),
          { status: 200, headers: { 'Content-Type': 'application/json' } }
        );
      });

      const res = await saveDeviceConfig('192.168.1.50', sampleBaseConfig, 0x1234ABCD);
      expect(sentBody).toBeDefined();
      expect(sentBody.expectedChecksum).toBe(0x1234ABCD);
      expect(res.accepted).toBe(true);
      expect(res.checksum).toBe(0x9999);
    });

    it('stale checksum surfaces ConfigConflictError on 409', async () => {
      vi.spyOn(transport, 'apiFetch').mockImplementation(async () => {
        return new Response(
          JSON.stringify({
            error: 'config_conflict',
            currentChecksum: 0xCAFEFACE,
          }),
          { status: 409, headers: { 'Content-Type': 'application/json' } }
        );
      });

      let caughtErr: any = null;
      try {
        await saveDeviceConfig('192.168.1.50', sampleBaseConfig, 0x1111);
      } catch (e) {
        caughtErr = e;
      }

      expect(caughtErr).toBeInstanceOf(ConfigConflictError);
      expect(caughtErr.currentChecksum).toBe(0xCAFEFACE);
      expect(caughtErr.message).toContain('config_conflict');
    });
  });

  describe('F: Validate endpoint', () => {
    it('validateDeviceConfig calls POST /api/v1/config/validate with candidate body and expectedChecksum', async () => {
      let calledPath = '';
      let sentBody: any = null;
      vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, path, init) => {
        calledPath = path;
        sentBody = JSON.parse(init?.body as string);
        return {
          valid: true,
          currentChecksum: 0x1234ABCD,
          impact: {
            hotApply: false,
            outputRestart: true,
            dmxRestart: true,
            syncRestart: false,
            schedulerRestart: false,
            mqttRestart: false,
            rebootRequired: false,
          },
          warnings: ['Outputs will blackout momentarily'],
        };
      });

      const res = await validateDeviceConfig('192.168.1.50', sampleBaseConfig, 0x1234ABCD);
      expect(calledPath).toBe('/api/v1/config/validate');
      expect(sentBody.expectedChecksum).toBe(0x1234ABCD);
      expect(res.valid).toBe(true);
      expect(res.impact.outputRestart).toBe(true);
      expect(res.impact.rebootRequired).toBe(false);
    });
  });
});
