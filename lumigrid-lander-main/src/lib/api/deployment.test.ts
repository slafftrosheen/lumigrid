import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { runPreflightCheck, executeStagedDeployment, type DeploymentPlan } from './deployment';
import * as nodeApi from './node';
import * as firmwareApi from './firmware';
import { nodeAuthStore } from '../auth/nodeAuth';

const ips = ['192.168.1.51', '192.168.1.52', '192.168.1.53', '192.168.1.54', '192.168.1.55'];
const idFor = (ip: string) => `lg-${ips.indexOf(ip) + 1}`;
const idle = (ip = ips[0]): firmwareApi.OtaStatus => ({ nodeId: idFor(ip), targetBinding: true,
  state: 'idle', runningVersion: '1.2.0', rolledBack: false, maxFirmwareBytes: 2000000,
  runningPartition: 'ota_0', nextPartition: 'ota_1' });
const receipt = (ip: string): firmwareApi.OtaUploadResponse => ({ accepted: true, commandId: 10, jobId: 1,
  targetVersion: '1.2.0', projectName: 'lumigrid', state: 'pending_reboot', nodeId: idFor(ip), targetPartition: 'ota_1' });
const confirmed = (ip: string): firmwareApi.OtaStatus => ({ ...idle(ip), state: 'confirmed',
  runningPartition: 'ota_1', nextPartition: 'ota_0', targetVersion: '1.2.0', targetPartition: 'ota_1' });
const capabilities = (ip: string) => ({ schema: 1,
  node: { family: 'led-node', model: 'lg-led-v1', firmware: '1.2.0', hardwareRevision: 'A' },
  limits: {}, features: { ota: true, signedOta: false }, protocols: {} });
const health = { system: { free_heap: 50000 }, output: { fault: false } };
const shortWait = { verifyDelayMs: 1, verifyTimeoutMs: 120 };

function setup(count = 3) {
  const plan: DeploymentPlan = { id: 'dep-1', targetVersion: '', nodeIds: ips.slice(0, count).map(idFor),
    canaryCount: 1, concurrency: 2, state: 'preflight' };
  const addresses = new Map(ips.slice(0, count).map(ip => [idFor(ip), ip]));
  const caps = vi.spyOn(nodeApi, 'getCapabilities').mockImplementation(async ip => capabilities(ip) as any);
  const healthy = vi.spyOn(nodeApi, 'getHealth').mockResolvedValue(health as any);
  const uploaded = new Set<string>();
  const status = vi.fn(async (ip: string) => idle(ip));
  const upload = vi.fn(async (ip: string, _binary: Blob | ArrayBuffer, _progress?: (pct: number) => void,
    _options?: { signal?: AbortSignal; expectedNodeId?: string }) => receipt(ip));
  const verify = vi.fn(async (ip: string, _nodeId: string, _signal?: AbortSignal) => confirmed(ip));
  vi.spyOn(firmwareApi, 'uploadFirmware').mockImplementation(async (...args) => {
    const result = await upload(...args); uploaded.add(args[0]); return result;
  });
  vi.spyOn(firmwareApi, 'getVerifiedOtaStatus').mockImplementation(async (ip, nodeId, signal) => {
    if (uploaded.has(ip)) return verify(ip, nodeId, signal);
    const result = await status(ip);
    if (result.nodeId !== undefined && result.nodeId !== nodeId) throw new firmwareApi.OtaIdentityError();
    return result;
  });
  const run = (options = shortWait, callback?: Parameters<typeof executeStagedDeployment>[3]) =>
    executeStagedDeployment(plan, addresses, new Blob(['firmware-image']), callback, options);
  return { plan, addresses, caps, healthy, status, upload, verify, run };
}

beforeEach(() => { vi.restoreAllMocks(); nodeAuthStore.clearAllSessions(); });
afterEach(() => { vi.restoreAllMocks(); nodeAuthStore.clearAllSessions(); });

describe('fleet preflight', () => {
  it('requires supported OTA, verified node identity, a valid slot and healthy outputs', async () => {
    setup(); const check = await runPreflightCheck('lg-1', ips[0], 'led-node', { imageBytes: 100 });
    expect(check.ok).toBe(true); expect(check.errors).toEqual([]);
    expect(check.status?.nextPartition).toBe('ota_1');
  });

  it('halts before upload when capability reads are unavailable', async () => {
    const f = setup(); f.caps.mockRejectedValue(new Error('network unavailable'));
    const result = await f.run();
    expect(result.success).toBe(false); expect(f.upload).not.toHaveBeenCalled();
  });

  it('rejects a different node now using the saved address', async () => {
    const f = setup(); f.status.mockRejectedValue(new firmwareApi.OtaIdentityError());
    const result = await f.run(); expect(result.success).toBe(false); expect(f.upload).not.toHaveBeenCalled();
  });

  it.each([
    ['unknown running partition', { runningPartition: 'unknown' }],
    ['same running and next partition', { nextPartition: 'ota_0' }],
    ['missing capacity', { maxFirmwareBytes: undefined }],
    ['too small slot', { maxFirmwareBytes: 1 }],
    ['pending update', { state: 'pending_verify' }],
    ['status identity mismatch', { nodeId: 'other' }],
  ])('does not write with %s', async (_description, change) => {
    const f = setup(); f.status.mockResolvedValue({ ...idle(), ...change } as firmwareApi.OtaStatus);
    expect((await f.run()).success).toBe(false); expect(f.upload).not.toHaveBeenCalled();
  });

  it('reports both low heap and an active output fault', async () => {
    const f = setup(); f.healthy.mockResolvedValue({ system: { free_heap: 10000 }, output: { fault: true } } as any);
    const check = await runPreflightCheck('lg-1', ips[0]);
    expect(check.ok).toBe(false); expect(check.errors.some(e => e.includes('free heap'))).toBe(true);
    expect(check.errors.some(e => e.includes('output fault'))).toBe(true);
  });

  it('does not infer OTA support or the supported family', async () => {
    const f = setup(); f.caps.mockResolvedValue({ ...capabilities(ips[0]), features: {}, node: { ...capabilities(ips[0]).node, family: 'other' } } as any);
    const check = await runPreflightCheck('lg-1', ips[0]);
    expect(check.ok).toBe(false); expect(check.errors).toHaveLength(2);
  });

  it('rechecks the target just before writing after initial fleet preflight', async () => {
    const f = setup(2); let calls = 0;
    f.status.mockImplementation(async ip => {
      if (ip === ips[0] && ++calls === 2) throw new firmwareApi.OtaIdentityError();
      return idle(ip);
    });
    expect((await f.run()).success).toBe(false); expect(f.upload).not.toHaveBeenCalled();
  });
});

describe('staged candidate confirmation', () => {
  it('completes only after the canary and all nodes confirm the accepted candidate', async () => {
    const f = setup(); const events: string[] = [];
    f.upload.mockImplementation(async ip => { events.push(`upload:${ip}`); return receipt(ip); });
    f.verify.mockImplementation(async ip => { events.push(`confirm:${ip}`); return confirmed(ip); });
    const progress: string[] = [];
    const result = await f.run(shortWait, p => progress.push(p.state));
    expect(result).toEqual({ success: true, completed: ['lg-1', 'lg-2', 'lg-3'], failed: [] });
    expect(events.indexOf(`confirm:${ips[0]}`)).toBeLessThan(events.indexOf(`upload:${ips[1]}`));
    expect(progress).toEqual(['preflight', 'canary', 'verifying', 'deploying', 'completed']);
    expect(f.upload).toHaveBeenCalledWith(ips[0], expect.any(Blob), undefined, { signal: undefined, expectedNodeId: 'lg-1' });
  });

  it('does not send the identity header to old firmware that lacks CORS support for it', async () => {
    const f = setup(1); f.status.mockResolvedValue({ ...idle(), nodeId: undefined, targetBinding: undefined });
    f.upload.mockResolvedValue({ ...receipt(ips[0]), nodeId: undefined, targetPartition: undefined });
    expect((await f.run()).success).toBe(true);
    expect(f.upload.mock.calls[0][3]?.expectedNodeId).toBeUndefined();
    expect(f.verify).toHaveBeenCalledWith(ips[0], 'lg-1', expect.any(AbortSignal));
  });

  it('waits past an older same-version confirmation from the other partition', async () => {
    const f = setup(1);
    f.verify.mockResolvedValueOnce({ ...confirmed(ips[0]), targetPartition: 'ota_0', runningPartition: 'ota_0' });
    expect((await f.run()).success).toBe(true); expect(f.verify).toHaveBeenCalledTimes(2);
  });

  it('keeps retrying after a transport request timeout within the verification deadline', async () => {
    const f = setup(1); f.verify.mockRejectedValueOnce(new DOMException('request timeout', 'AbortError'));
    expect((await f.run()).success).toBe(true); expect(f.verify).toHaveBeenCalledTimes(2);
  });

  it('does not replace unavailable OTA status with healthy output or matching version', async () => {
    const f = setup(); f.verify.mockRejectedValue(new Error('status unavailable'));
    const result = await f.run({ verifyDelayMs: 1, verifyTimeoutMs: 25 });
    expect(result.success).toBe(false); expect(result.completed).toEqual([]); expect(f.upload).toHaveBeenCalledOnce();
  });

  it('stops when the verification identity changes', async () => {
    const f = setup(); f.verify.mockRejectedValue(new firmwareApi.OtaIdentityError());
    const result = await f.run(); expect(result.success).toBe(false); expect(f.upload).toHaveBeenCalledOnce();
    expect(f.verify).toHaveBeenCalledOnce();
  });

  it('halts on a canary rollback before later uploads', async () => {
    const f = setup(); f.verify.mockResolvedValue({ ...confirmed(ips[0]), state: 'rolled_back', rolledBack: true, runningPartition: 'ota_0' });
    expect(await f.run()).toEqual({ success: false, completed: [], failed: ['lg-1'] });
    expect(f.upload).toHaveBeenCalledOnce(); expect(f.plan.state).toBe('failed');
  });

  it('halts after a failed batch without starting the following batch', async () => {
    const f = setup(5); f.verify.mockImplementation(async ip => ({ ...confirmed(ip),
      ...(ip === ips[1] ? { state: 'failed' as const, reason: 'startup verification failed' } : {}) }));
    const result = await f.run();
    expect(result).toEqual({ success: false, completed: ['lg-1', 'lg-3'], failed: ['lg-2'] });
    expect(f.upload.mock.calls.map(call => call[0])).toEqual(ips.slice(0, 3));
  });

  it('honors multiple sequential canaries before the fleet batch', async () => {
    const f = setup(4); f.plan.canaryCount = 2; const events: string[] = [];
    f.upload.mockImplementation(async ip => { events.push(`upload:${ip}`); return receipt(ip); });
    f.verify.mockImplementation(async ip => { events.push(`confirm:${ip}`); return confirmed(ip); });
    expect((await f.run()).success).toBe(true);
    expect(events.indexOf(`confirm:${ips[0]}`)).toBeLessThan(events.indexOf(`upload:${ips[1]}`));
    expect(events.indexOf(`confirm:${ips[1]}`)).toBeLessThan(events.indexOf(`upload:${ips[2]}`));
  });

  it('uses the accepted image version when the optional input is blank', async () => {
    const f = setup(1);
    f.upload.mockResolvedValue({ ...receipt(ips[0]), targetVersion: '1.3.0' });
    f.verify.mockResolvedValue({ ...confirmed(ips[0]), runningVersion: '1.3.0', targetVersion: '1.3.0' });
    expect((await f.run()).success).toBe(true);
  });

  it('halts when an accepted version differs from the requested candidate', async () => {
    const f = setup(); f.plan.targetVersion = '1.3.0';
    expect((await f.run()).success).toBe(false); expect(f.verify).not.toHaveBeenCalled();
    expect(f.upload).toHaveBeenCalledOnce();
  });

  it('retains the submitted IDs, addresses and concurrency while awaiting the canary', async () => {
    const f = setup(3);
    f.verify.mockImplementation(async ip => {
      if (ip === ips[0]) {
        f.addresses.set('lg-2', '192.168.1.200'); f.plan.nodeIds.push('other'); f.plan.concurrency = 0;
      }
      return confirmed(ip);
    });
    expect((await f.run()).success).toBe(true);
    expect(f.upload.mock.calls.map(call => call[0])).toEqual(ips.slice(0, 3));
  });

  it('stops later uploads when the page aborts after a receipt', async () => {
    const f = setup(), controller = new AbortController();
    const result = await f.run({ ...shortWait, signal: controller.signal } as any, progress => {
      if (progress.state === 'verifying') controller.abort();
    });
    expect(result.success).toBe(false); expect(f.upload).toHaveBeenCalledOnce(); expect(f.verify).not.toHaveBeenCalled();
  });

  it('does not confirm or continue an upload after its credentials change', async () => {
    const f = setup();
    nodeAuthStore.setSession({ address: ips[0], nodeId: 'lg-1', role: 'admin', sessionToken: 'old', expiresAt: Date.now() + 60000 });
    f.upload.mockImplementation(async ip => {
      nodeAuthStore.setSession({ address: ip, nodeId: idFor(ip), role: 'admin', sessionToken: 'new', expiresAt: Date.now() + 60000 });
      return receipt(ip);
    });
    expect((await f.run()).success).toBe(false); expect(f.upload).toHaveBeenCalledOnce(); expect(f.verify).not.toHaveBeenCalled();
  });

  it.each([0, -1, 1.5, Number.NaN])('rejects invalid concurrency %s without writing', async concurrency => {
    const f = setup(); f.plan.concurrency = concurrency;
    expect((await f.run()).success).toBe(false); expect(f.upload).not.toHaveBeenCalled();
  });
});
