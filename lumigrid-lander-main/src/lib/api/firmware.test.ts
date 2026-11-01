import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  uploadFirmware,
  getOtaStatus,
  isOtaSuccess,
  isOtaRolledBack,
  bindOtaTarget, getVerifiedOtaStatus, isOtaTargetStatus, isOtaSlotReady,
  firmwareFitsSlot, waitForOtaDelay, OtaUnconfirmedError, OtaIdentityError,
  type OtaStatus,
} from './firmware';
import * as transport from './transport';
import { nodeAuthStore } from '../auth/nodeAuth';

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

describe('OTA receipt and candidate ownership', () => {
  const ip = '192.168.1.100';
  const receipt = { accepted: true, commandId: 10, jobId: 1, targetVersion: '1.2.0',
    projectName: 'lumigrid_led_node', state: 'pending_reboot' as const, nodeId: 'lg-01', targetPartition: 'ota_1' };
  const before: OtaStatus = { state: 'idle', runningVersion: '1.2.0', rolledBack: false,
    maxFirmwareBytes: 2000000, runningPartition: 'ota_0', nextPartition: 'ota_1' };
  const target = { nodeId: 'lg-01', targetVersion: '1.2.0', targetPartition: 'ota_1' };
  const confirmed: OtaStatus = { ...before, ...target, state: 'confirmed', runningPartition: 'ota_1', nextPartition: 'ota_0' };
  const session = (token: string) => ({ address: ip, nodeId: 'lg-01', role: 'admin' as const,
    sessionToken: token, expiresAt: Date.now() + 60000 });

  function xhrFixture() {
    const xhr: any = { upload: { onprogress: null }, status: 200, statusText: 'OK', responseText: JSON.stringify(receipt),
      open: vi.fn(), setRequestHeader: vi.fn(), send: vi.fn(), abort: vi.fn() };
    vi.stubGlobal('XMLHttpRequest', vi.fn(() => xhr));
    return xhr;
  }

  beforeEach(() => { vi.restoreAllMocks(); nodeAuthStore.clearAllSessions(); });
  afterEach(() => { vi.unstubAllGlobals(); vi.useRealTimers(); nodeAuthStore.clearAllSessions(); });

  it.each([
    ['non-JSON', 'accepted'], ['false acceptance', { ...receipt, accepted: false }],
    ['minimal allocation fallback', { accepted: true, state: 'pending_reboot' }],
    ['missing version', { ...receipt, targetVersion: '' }], ['invalid command ID', { ...receipt, commandId: 0 }],
    ['invalid job ID', { ...receipt, jobId: 1.5 }], ['wrong state', { ...receipt, state: 'confirmed' }],
  ])('keeps a 2xx %s receipt unconfirmed', async (_name, body) => {
    const xhr = xhrFixture();
    xhr.responseText = typeof body === 'string' ? body : JSON.stringify(body);
    const pending = uploadFirmware(ip, new Blob(['bin']));
    const rejected = expect(pending).rejects.toBeInstanceOf(OtaUnconfirmedError);
    xhr.onload(); await rejected;
  });

  it('pins the expected node in the request and requires it in the receipt', async () => {
    const xhr = xhrFixture();
    const pending = uploadFirmware(ip, new Blob(['bin']), undefined, { expectedNodeId: 'lg-01' });
    expect(xhr.setRequestHeader).toHaveBeenCalledWith('X-LumiGrid-Node-Id', 'lg-01');
    xhr.onload(); await expect(pending).resolves.toEqual(receipt);
  });

  it('does not accept a receipt from another node', async () => {
    const xhr = xhrFixture(); xhr.responseText = JSON.stringify({ ...receipt, nodeId: 'lg-other' });
    const pending = uploadFirmware(ip, new Blob(['bin']), undefined, { expectedNodeId: 'lg-01' });
    const rejected = expect(pending).rejects.toBeInstanceOf(OtaUnconfirmedError);
    xhr.onload(); await rejected;
  });

  it('does not clear replacement credentials on a late 401 response', async () => {
    nodeAuthStore.setSession(session('old'));
    const xhr = xhrFixture();
    const pending = uploadFirmware(ip, new Blob(['bin']));
    nodeAuthStore.setSession(session('new'));
    xhr.status = 401;
    const rejected = expect(pending).rejects.toMatchObject({ name: 'AbortError' });
    xhr.onload(); await rejected;
    expect(nodeAuthStore.getSession(ip)?.sessionToken).toBe('new');
  });

  it('clears the matching session on a current 401 response', async () => {
    nodeAuthStore.setSession(session('old'));
    const xhr = xhrFixture(); xhr.status = 401;
    const pending = uploadFirmware(ip, new Blob(['bin']));
    const rejected = expect(pending).rejects.toThrow('Authentication required');
    xhr.onload(); await rejected;
    expect(nodeAuthStore.getSession(ip)).toBeNull();
  });

  it('aborts the XHR, settles once and detaches callbacks', async () => {
    const controller = new AbortController(), xhr = xhrFixture();
    const progress = vi.fn();
    const pending = uploadFirmware(ip, new Blob(['bin']), progress, { signal: controller.signal });
    const lateLoad = xhr.onload;
    const rejected = expect(pending).rejects.toMatchObject({ name: 'AbortError' });
    controller.abort(); await rejected;
    expect(xhr.abort).toHaveBeenCalledOnce();
    expect(xhr.onload).toBeNull(); expect(xhr.upload.onprogress).toBeNull();
    lateLoad();
    expect(progress).not.toHaveBeenCalled();
  });

  it('never sends an already cancelled request', async () => {
    const controller = new AbortController(); controller.abort(); const xhr = xhrFixture();
    await expect(uploadFirmware(ip, new Blob(['bin']), undefined, { signal: controller.signal })).rejects.toMatchObject({ name: 'AbortError' });
    expect(xhr.send).not.toHaveBeenCalled();
  });

  it('limits upload progress to 90 and ignores a replaced session', async () => {
    nodeAuthStore.setSession(session('old'));
    const xhr = xhrFixture(), progress = vi.fn();
    const pending = uploadFirmware(ip, new Blob(['bin']), progress);
    xhr.upload.onprogress({ lengthComputable: true, loaded: 120, total: 100 });
    expect(progress).toHaveBeenLastCalledWith(90);
    nodeAuthStore.setSession(session('new'));
    xhr.upload.onprogress({ lengthComputable: true, loaded: 50, total: 100 });
    expect(progress).toHaveBeenCalledOnce();
    const rejected = expect(pending).rejects.toMatchObject({ name: 'AbortError' }); xhr.onload(); await rejected;
  });

  it('binds legacy receipts to the previously reported next partition', () => {
    expect(bindOtaTarget({ ...receipt, nodeId: undefined, targetPartition: undefined }, before, 'lg-01')).toEqual(target);
    expect(() => bindOtaTarget(receipt, before, 'lg-other')).toThrow(OtaIdentityError);
    expect(() => bindOtaTarget(receipt, { ...before, runningPartition: 'unknown' }, 'lg-01')).toThrow(OtaUnconfirmedError);
  });

  it('does not mistake an earlier same-version confirmation for this candidate', () => {
    expect(isOtaSuccess(confirmed, target)).toBe(true);
    expect(isOtaSuccess({ ...confirmed, targetPartition: 'ota_0', runningPartition: 'ota_0' }, target)).toBe(false);
    expect(isOtaSuccess({ ...confirmed, runningPartition: 'ota_0' }, target)).toBe(false);
    expect(isOtaSuccess({ ...confirmed, nodeId: 'lg-other' }, target)).toBe(false);
    expect(isOtaSuccess({ ...confirmed, rolledBack: true }, target)).toBe(false);
    expect(isOtaTargetStatus({ ...confirmed, targetPartition: undefined }, target)).toBe(false);
  });

  it('verifies legacy status with a fresh identity read and no cached responses', async () => {
    const request = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, path) =>
      path.endsWith('/identity') ? { id: 'lg-01' } : { ...confirmed, nodeId: undefined });
    await expect(getVerifiedOtaStatus(ip, 'lg-01')).resolves.toMatchObject({ nodeId: 'lg-01', state: 'confirmed' });
    expect(request).toHaveBeenCalledWith(ip, '/api/v1/identity', { signal: undefined, cache: 'no-store' });
  });

  it.each([{}, { id: 'lg-other' }])('rejects a missing or changed identity %j', async identity => {
    vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, path) => path.endsWith('/identity') ? identity : confirmed);
    await expect(getVerifiedOtaStatus(ip, 'lg-01')).rejects.toBeInstanceOf(OtaIdentityError);
  });

  it('does not use a successful identity read as a substitute for missing OTA status', async () => {
    vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, path) => {
      if (path.endsWith('/identity')) return { id: 'lg-01' };
      throw new Error('status unavailable');
    });
    await expect(getVerifiedOtaStatus(ip, 'lg-01')).rejects.toThrow('status unavailable');
  });

  it.each([0, -1, Number.NaN, Number.POSITIVE_INFINITY, 1.5])('rejects invalid image or slot size %s', size => {
    expect(firmwareFitsSlot(size, 2000000)).toBe(false);
    expect(firmwareFitsSlot(100, size)).toBe(false);
    expect(isOtaSlotReady({ ...before, maxFirmwareBytes: size })).toBe(false);
  });

  it('cancels a verification delay and releases its timer', async () => {
    vi.useFakeTimers(); const controller = new AbortController();
    const pending = waitForOtaDelay(90000, controller.signal);
    const rejected = expect(pending).rejects.toMatchObject({ name: 'AbortError' });
    controller.abort(); await rejected;
    expect(vi.getTimerCount()).toBe(0);
  });
});
