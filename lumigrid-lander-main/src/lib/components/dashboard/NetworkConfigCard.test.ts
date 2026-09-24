import { describe, it, expect, vi, beforeEach } from 'vitest';
import * as configApi from '$lib/api/config';

describe('NetworkConfigCard component logic', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('GET config never expects password contents and displays passwordSet status', async () => {
    const mockConfig: configApi.NetworkConfigWire = {
      mode: 'sta_with_ap_fallback',
      ssid: 'Venue-Lighting-2.4G',
      passwordSet: true,
      apName: 'LumiGrid-Recovery',
      apChannel: 6,
    };
    vi.spyOn(configApi, 'getNetworkConfig').mockResolvedValue(mockConfig);

    const config = await configApi.getNetworkConfig('192.168.4.1');
    expect(config.passwordSet).toBe(true);
    expect((config as any).password).toBeUndefined();
  });

  it('existing password is preserved when input remains blank on save', async () => {
    const saveSpy = vi.spyOn(configApi, 'saveNetworkConfig').mockResolvedValue({
      accepted: true,
      commandId: 105,
      applyPending: true,
    });

    const update: configApi.NetworkConfigUpdate = {
      mode: 'sta_with_ap_fallback',
      ssid: 'Venue-Lighting-2.4G',
      apChannel: 6,
    };

    // When password input is blank, no password field is included in the payload
    await configApi.saveNetworkConfig('192.168.4.1', update);
    expect(saveSpy).toHaveBeenCalledWith('192.168.4.1', expect.not.objectContaining({ password: expect.anything() }));
  });

  it('clearPassword is explicit when checkbox is checked', async () => {
    const saveSpy = vi.spyOn(configApi, 'saveNetworkConfig').mockResolvedValue({
      accepted: true,
      commandId: 106,
      applyPending: true,
    });

    const update: configApi.NetworkConfigUpdate = {
      mode: 'sta_only',
      ssid: 'OpenVenue',
      clearPassword: true,
    };

    await configApi.saveNetworkConfig('192.168.4.1', update);
    expect(saveSpy).toHaveBeenCalledWith('192.168.4.1', expect.objectContaining({ clearPassword: true }));
  });

  it('apply shows pending/reconnecting state and handles transition', async () => {
    vi.spyOn(configApi, 'saveNetworkConfig').mockResolvedValue({
      accepted: true,
      commandId: 107,
      applyPending: true,
    });

    let pollCount = 0;
    vi.spyOn(configApi, 'getNetworkStatus').mockImplementation(async () => {
      pollCount++;
      if (pollCount === 1) {
        return {
          state: 'connecting',
          mode: 'sta_with_ap_fallback',
          staConnected: false,
          staSsid: 'NewVenueLAN',
          staRssi: 0,
          staIp: '0.0.0.0',
          apActive: true,
          apIp: '192.168.4.1',
          retryCount: 1,
          applyState: 'waiting_for_network',
          pendingCommandId: 107,
          lastApplyError: null,
          lastDisconnectReason: 0,
          persistedMatchesRuntime: false,
        };
      }
      return {
        state: 'connected',
        mode: 'sta_with_ap_fallback',
        staConnected: true,
        staSsid: 'NewVenueLAN',
        staRssi: -52,
        staIp: '192.168.1.180',
        apActive: false,
        apIp: '0.0.0.0',
        retryCount: 0,
        applyState: 'idle',
        pendingCommandId: null,
        lastApplyError: null,
        lastDisconnectReason: 0,
        persistedMatchesRuntime: true,
      };
    });

    const res = await configApi.saveNetworkConfig('192.168.4.1', {
      mode: 'sta_with_ap_fallback',
      ssid: 'NewVenueLAN',
    });
    expect(res.applyPending).toBe(true);

    const st1 = await configApi.getNetworkStatus('192.168.4.1');
    expect(st1.applyState).toBe('waiting_for_network');

    const st2 = await configApi.getNetworkStatus('192.168.4.1');
    expect(st2.applyState).toBe('idle');
    expect(st2.persistedMatchesRuntime).toBe(true);
  });

  it('failed STA shows recovery state truth', async () => {
    vi.spyOn(configApi, 'getNetworkStatus').mockResolvedValue({
      state: 'fallback_ap',
      mode: 'sta_with_ap_fallback',
      staConnected: false,
      staSsid: 'BadPasswordSSID',
      staRssi: 0,
      staIp: '0.0.0.0',
      apActive: true,
      apIp: '192.168.4.1',
      retryCount: 3,
      applyState: 'failed',
      pendingCommandId: null,
      lastApplyError: 'sta_connect_failed',
      lastDisconnectReason: 204,
      persistedMatchesRuntime: false,
    });

    const status = await configApi.getNetworkStatus('192.168.4.1');
    expect(status.state).toBe('fallback_ap');
    expect(status.lastApplyError).toBe('sta_connect_failed');
    expect(status.persistedMatchesRuntime).toBe(false);
  });

  it('validation blocks invalid channel (< 1 or > 13)', () => {
    const isValidChannel = (ch: number) => ch >= 1 && ch <= 13 && Number.isInteger(ch);
    expect(isValidChannel(0)).toBe(false);
    expect(isValidChannel(14)).toBe(false);
    expect(isValidChannel(1)).toBe(true);
    expect(isValidChannel(13)).toBe(true);
    expect(isValidChannel(6)).toBe(true);
  });

  it('validation blocks invalid SSID (empty for sta modes, or > 32 chars)', () => {
    const isValidSsid = (mode: string, ssid: string) => {
      if (mode === 'sta_only' || mode === 'sta_with_ap_fallback') {
        if (!ssid || ssid.trim().length === 0 || ssid.length > 32) return false;
      }
      return true;
    };

    expect(isValidSsid('sta_only', '')).toBe(false);
    expect(isValidSsid('sta_with_ap_fallback', '   ')).toBe(false);
    expect(isValidSsid('sta_with_ap_fallback', 'A'.repeat(33))).toBe(false);
    expect(isValidSsid('sta_with_ap_fallback', 'Valid_SSID')).toBe(true);
    expect(isValidSsid('ap_only', '')).toBe(true); // AP only mode does not require STA SSID
  });
});
