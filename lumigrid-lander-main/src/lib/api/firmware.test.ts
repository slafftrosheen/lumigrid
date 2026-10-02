import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  uploadFirmware,
  getOtaStatus,
  isOtaSuccess,
  isOtaRolledBack,
  type OtaStatus,
} from './firmware';
import * as transport from './transport';

describe('firmware & OTA contracts (LG-036.1)', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  describe('OTA status evaluation (Requirements AE, AF, AG)', () => {
    it('AE: confirmed target version evaluates to success', () => {
      const status: OtaStatus = {
        state: 'confirmed',
        runningVersion: '1.2.0',
        targetVersion: '1.2.0',
        rolledBack: false,
      };
      expect(isOtaSuccess(status)).toBe(true);
      expect(isOtaRolledBack(status)).toBe(false);
    });

    it('AF & AG: old version after rollback is NEVER success', () => {
      const status: OtaStatus = {
        state: 'rolled_back',
        runningVersion: '1.0.0',
        targetVersion: '1.2.0',
        previousVersion: '1.0.0',
        rolledBack: true,
        reason: 'startup_self_test_failed',
      };
      expect(isOtaSuccess(status)).toBe(false);
      expect(isOtaRolledBack(status)).toBe(true);
    });

    it('pending_reboot or pending_verify is not yet success', () => {
      const rebooting: OtaStatus = {
        state: 'pending_reboot',
        runningVersion: '1.0.0',
        targetVersion: '1.2.0',
        rolledBack: false,
      };
      expect(isOtaSuccess(rebooting)).toBe(false);

      const verifying: OtaStatus = {
        state: 'pending_verify',
        runningVersion: '1.2.0',
        targetVersion: '1.2.0',
        rolledBack: false,
      };
      expect(isOtaSuccess(verifying)).toBe(false);
    });
  });

  describe('Transport & Endpoint Invariants (Requirements Y, Z, AA, AB, AC)', () => {
    it('AA & AB: OTA URL uses getNodeHttpBase and never promotes HTTP node to HTTPS based on page origin', async () => {
      // Simulate dashboard running on https://cloud.lumigrid.io
      const mockBase = 'http://192.168.1.100';
      vi.spyOn(transport, 'getNodeHttpBase').mockReturnValue(mockBase);

      let openedMethod = '';
      let openedUrl = '';
      const mockXhr = {
        upload: { onprogress: null },
        open: vi.fn((method, url) => {
          openedMethod = method;
          openedUrl = url;
        }),
        setRequestHeader: vi.fn(),
        send: vi.fn(function (this: any) {
          this.status = 200;
          this.responseText = JSON.stringify({
            accepted: true,
            commandId: 10,
            jobId: 1,
            targetVersion: '1.2.0',
            projectName: 'lumigrid_led_node',
            state: 'pending_reboot',
          });
          this.onload();
        }),
      };

      vi.stubGlobal('XMLHttpRequest', vi.fn(() => mockXhr));

      const res = await uploadFirmware('192.168.1.100', new Blob(['fake-bin']));

      // Must call POST /api/v1/ota on http://192.168.1.100
      expect(openedMethod).toBe('POST');
      expect(openedUrl).toBe('http://192.168.1.100/api/v1/ota');
      expect(openedUrl).not.toContain('/update');
      expect(openedUrl.startsWith('http://')).toBe(true);

      // AC: targetVersion comes from node response metadata, not filename
      expect(res.targetVersion).toBe('1.2.0');
      expect(res.state).toBe('pending_reboot');
    });

    it('fetches authoritative OTA status via GET /api/v1/ota/status', async () => {
      let calledPath = '';
      vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, path) => {
        calledPath = path;
        return {
          state: 'confirmed',
          runningVersion: '1.2.0',
          targetVersion: '1.2.0',
          rolledBack: false,
        };
      });

      const status = await getOtaStatus('192.168.1.100');
      expect(calledPath).toBe('/api/v1/ota/status');
      expect(status.state).toBe('confirmed');
    });
  });
});
