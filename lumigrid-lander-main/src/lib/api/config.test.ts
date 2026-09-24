import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  normalizeDeviceConfig,
  serializeDeviceConfig,
  getDeviceConfig,
  saveDeviceConfig,
  getNetworkConfig,
  saveNetworkConfig,
  getNetworkStatus,
  type DeviceConfigWire,
  type DeviceConfig,
} from './config';
import * as transport from './transport';

describe('config client contract', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  describe('normalizeDeviceConfig', () => {
    it('normalizes canonical firmware public wire format', () => {
      const canonicalWire: DeviceConfigWire = {
        deviceName: 'lumigrid-node-01',
        serialNumber: 'LG-2026-001',
        hardwareRevision: 'v1.0',
        siteLabel: 'West Wing',
        groupLabel: 'Main Façade',
        activePreset: 3,
        fallbackPreset: 1,
        watchdogTimeoutS: 30,
        maxPowerMw: 45000,
        gamma: 2.2,
        softStart: true,
        softStartMs: 750,
        timezone: 'America/New_York',
        syncEnabled: true,
        syncRole: 1,
        syncToken: 42,
        mqttEnabled: true,
        mqttBroker: 'mqtt.internal',
        mqttPort: 1883,
        channels: [
          {
            name: 'A1 Strip',
            enabled: true,
            maxBrightness: 240,
            gammaCorrection: true,
            maxMilliwatts: 10000,
            dmxUniverse: 1,
            dmxStartChannel: 1,
            dmxMode: 0,
            port: 'A1',
            kind: 'addressable',
            hardwareId: 0,
            gpio: 2,
            configured: true,
            addressable: {
              type: 'SK6812',
              pixels: 120,
              pixelOrder: 'GRBW',
            },
          },
        ],
        groups: [
          {
            index: 0,
            name: 'Façade Upper',
            channelMask: 0x0001,
            role: 'primary',
            enabled: true,
          },
        ],
      };

      const normalized = normalizeDeviceConfig(canonicalWire);

      expect(normalized.deviceName).toBe('lumigrid-node-01');
      expect(normalized.serialNumber).toBe('LG-2026-001');
      expect(normalized.hardwareRevision).toBe('v1.0');
      expect(normalized.siteLabel).toBe('West Wing');
      expect(normalized.groupLabel).toBe('Main Façade');
      expect(normalized.activePresetIndex).toBe(3);
      expect(normalized.fallbackPresetIndex).toBe(1);
      expect(normalized.gammaX10).toBe(22);
      expect(normalized.softStartEnabled).toBe(true);
      expect(normalized.softStartMs).toBe(750);
      expect(normalized.syncToken).toBe(42);

      // Check channels addressable flattening/normalization
      expect(normalized.channels?.[0]?.name).toBe('A1 Strip');
      expect(normalized.channels?.[0]?.pixelCount).toBe(120);
      expect(normalized.channels?.[0]?.addressableType).toBe(1); // SK6812
      expect(normalized.channels?.[0]?.pixelOrder).toBe(3); // GRBW
    });

    it('normalizes compatibility wire format using old/internal aliases', () => {
      const legacyWire = {
        deviceName: 'lumigrid-legacy',
        activePresetIndex: 4,
        fallbackPresetIndex: 0,
        gammaX10: 24,
        softStartEnabled: false,
        softStartMs: 250,
        watchdogTimeoutS: 60,
        maxPowerMw: 0,
      } as unknown as DeviceConfigWire;

      const normalized = normalizeDeviceConfig(legacyWire);

      expect(normalized.deviceName).toBe('lumigrid-legacy');
      expect(normalized.activePresetIndex).toBe(4);
      expect(normalized.fallbackPresetIndex).toBe(0);
      expect(normalized.gammaX10).toBe(24);
      expect(normalized.softStartEnabled).toBe(false);
      expect(normalized.softStartMs).toBe(250);
    });
  });

  describe('serializeDeviceConfig', () => {
    it('serializes internal model into canonical public firmware wire keys', () => {
      const internalConfig: DeviceConfig = {
        deviceName: 'lumigrid-desk',
        serialNumber: 'SN-9988',
        hardwareRevision: 'v1.1',
        siteLabel: 'Tower',
        groupLabel: 'North',
        activePresetIndex: 2,
        fallbackPresetIndex: 0,
        watchdogTimeoutS: 45,
        maxPowerMw: 50000,
        gammaX10: 22,
        softStartEnabled: true,
        softStartMs: 500,
        mqttEnabled: false,
        mqttBroker: '',
        mqttPort: 1883,
        syncEnabled: false,
        syncRole: 0,
        protocol: {
          artnetEnabled: false,
          sacnEnabled: true,
          universeStart: 1,
          liveStreamLayer: 2,
        },
        channels: [
          {
            name: 'A1 Neon',
            enabled: true,
            addressableType: 0, // WS2812
            pixelOrder: 1, // GRB
            pixelCount: 60,
            maxBrightness: 255,
            gammaCorrection: true,
            maxMilliwatts: 15000,
            dmxUniverse: 1,
            dmxStartChannel: 1,
            dmxMode: 0,
          },
        ],
        groups: [
          {
            name: 'Roof',
            channelMask: 1,
            role: 0, // Primary
            enabled: true,
          },
        ],
      };

      const serialized = serializeDeviceConfig(internalConfig);

      // Verify canonical public keys exist
      expect(serialized).toHaveProperty('activePreset', 2);
      expect(serialized).toHaveProperty('fallbackPreset', 0);
      expect(serialized).toHaveProperty('gamma', 2.2);
      expect(serialized).toHaveProperty('softStart', true);
      expect(serialized).toHaveProperty('softStartMs', 500);
      expect(serialized).toHaveProperty('serialNumber', 'SN-9988');

      // Verify internal names are NOT emitted as root wire keys
      expect(serialized).not.toHaveProperty('activePresetIndex');
      expect(serialized).not.toHaveProperty('fallbackPresetIndex');
      expect(serialized).not.toHaveProperty('gammaX10');
      expect(serialized).not.toHaveProperty('softStartEnabled');

      // Verify channel addressable structure
      const ch0 = (serialized.channels as any[])[0];
      expect(ch0.addressable).toEqual({
        type: 'WS2812',
        pixels: 60,
        pixelOrder: 'GRB',
      });
    });
  });

  describe('getDeviceConfig and saveDeviceConfig network calls', () => {
    it('getDeviceConfig normalizes returned wire data', async () => {
      const mockWire = {
        deviceName: 'mock-node',
        activePreset: 5,
        fallbackPreset: 2,
        gamma: 2.0,
        softStart: false,
        softStartMs: 300,
        watchdogTimeoutS: 60,
        maxPowerMw: 20000,
      };

      vi.spyOn(transport, 'apiJson').mockResolvedValue(mockWire);

      const res = await getDeviceConfig('192.168.1.100');
      expect(res.deviceName).toBe('mock-node');
      expect(res.activePresetIndex).toBe(5);
      expect(res.fallbackPresetIndex).toBe(2);
      expect(res.gammaX10).toBe(20);
      expect(res.softStartEnabled).toBe(false);
    });

    it('saveDeviceConfig sends canonical JSON body', async () => {
      const apiSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ saved: true });

      const testConfig: DeviceConfig = {
        deviceName: 'node-live',
        activePresetIndex: 3,
        fallbackPresetIndex: 0,
        watchdogTimeoutS: 60,
        maxPowerMw: 30000,
        gammaX10: 22,
        softStartEnabled: true,
        softStartMs: 500,
        mqttEnabled: false,
        mqttBroker: '',
        mqttPort: 1883,
        syncEnabled: false,
        syncRole: 0,
        protocol: {
          artnetEnabled: false,
          sacnEnabled: false,
          universeStart: 1,
          liveStreamLayer: 2,
        },
      };

      await saveDeviceConfig('192.168.1.100', testConfig);

      expect(apiSpy).toHaveBeenCalledTimes(1);
      const [ip, path, options] = apiSpy.mock.calls[0];
      expect(ip).toBe('192.168.1.100');
      expect(path).toBe('/api/v1/config');
      expect(options?.method).toBe('PUT');

      const body = JSON.parse(options?.body as string);
      expect(body.activePreset).toBe(3);
      expect(body.fallbackPreset).toBe(0);
      expect(body.gamma).toBe(2.2);
      expect(body.softStart).toBe(true);
      expect(body.activePresetIndex).toBeUndefined();
      expect(body.gammaX10).toBeUndefined();
      expect(body.syncToken).toBeUndefined();
    });

    it('serializeDeviceConfig omits syncToken when 0 or undefined, but includes when valid', () => {
      const base: DeviceConfig = {
        deviceName: 'node-sync',
        activePresetIndex: 0,
        fallbackPresetIndex: 0,
        watchdogTimeoutS: 60,
        maxPowerMw: 1000,
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

      // Undefined token
      const p1 = serializeDeviceConfig(base);
      expect(p1.syncToken).toBeUndefined();

      // Zero token
      const p2 = serializeDeviceConfig({ ...base, syncToken: 0 });
      expect(p2.syncToken).toBeUndefined();

      // Non-zero token
      const p3 = serializeDeviceConfig({ ...base, syncToken: 12345678 });
      expect(p3.syncToken).toBe(12345678);
    });
  });

  describe('network api client', () => {
    it('getNetworkConfig calls GET /api/v1/network/config and returns network wire config without password', async () => {
      const mockConfig = {
        mode: 'sta_with_ap_fallback',
        ssid: 'Workshop-WiFi',
        passwordSet: true,
        apEnabled: true,
        apName: 'LumiGrid-Node',
        apChannel: 6,
      };
      const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue(mockConfig);

      const result = await getNetworkConfig('192.168.4.1');

      expect(apiJsonSpy).toHaveBeenCalledWith('192.168.4.1', '/api/v1/network/config');
      expect(result).toEqual(mockConfig);
      expect((result as any).password).toBeUndefined();
    });

    it('saveNetworkConfig calls PUT /api/v1/network/config with payload', async () => {
      const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
        saved: true,
        accepted: true,
        commandId: 101,
      });

      const update = {
        mode: 'sta_with_ap_fallback' as const,
        ssid: 'Installation-LAN',
        password: 'NewSecretPassword',
      };

      const result = await saveNetworkConfig('192.168.4.1', update);

      expect(apiJsonSpy).toHaveBeenCalledWith('192.168.4.1', '/api/v1/network/config', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(update),
      });
      expect(result.saved).toBe(true);
    });

    it('getNetworkStatus calls GET /api/v1/network/status and returns status', async () => {
      const mockStatus = {
        state: 'Connected',
        mode: 'sta_with_ap_fallback' as const,
        staConnected: true,
        apActive: true,
        staSsid: 'Workshop-WiFi',
        staRssi: -58,
        staIp: '192.168.1.150',
        apIp: '192.168.4.1',
        retryCount: 0,
      };
      const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue(mockStatus);

      const result = await getNetworkStatus('192.168.1.150');

      expect(apiJsonSpy).toHaveBeenCalledWith('192.168.1.150', '/api/v1/network/status');
      expect(result.staConnected).toBe(true);
      expect(result.staIp).toBe('192.168.1.150');
      expect((result as any).password).toBeUndefined();
    });
  });
});

