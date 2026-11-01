import { afterEach, describe, expect, it, vi } from 'vitest';
import { loadPageFunctions } from '../testing/pageScriptHarness';
import * as firmware from '../api/firmware';

function deferred<T = any>() {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

const slot: firmware.OtaStatus = { nodeId: 'node-a', targetBinding: true, state: 'idle',
  runningVersion: '1.2.0', maxFirmwareBytes: 2000000, runningPartition: 'ota_0', nextPartition: 'ota_1', rolledBack: false };
const receipt: firmware.OtaUploadResponse = { accepted: true, nodeId: 'node-a', commandId: 4, jobId: 1,
  targetVersion: '1.2.0', targetPartition: 'ota_1', state: 'pending_reboot', projectName: 'lumigrid' };
const target: firmware.OtaVerificationTarget = { nodeId: 'node-a', targetVersion: '1.2.0', targetPartition: 'ota_1' };
const confirmed: firmware.OtaStatus = { ...slot, ...target, state: 'confirmed', runningPartition: 'ota_1', nextPartition: 'ota_0' };

function otaWorkspace() {
  const context: Record<string, any> = {
    nodeIp: '192.168.1.50', nodeId: 'node-a', generation: 1, disposed: false, controller: null,
    nodeAuthStore: { getSession: vi.fn(() => null) }, phase: 'select', selectedFile: new File(['bin'], 'firmware.bin'),
    progress: 0, uploadResult: null, otaStatus: null, error: '', capacity: slot, capacityLoading: false,
    ...firmware, getVerifiedOtaStatus: vi.fn(async () => slot), uploadFirmware: vi.fn(async () => receipt),
  };
  Object.defineProperties(context, {
    capacityReady: { get: () => Boolean(context.capacity && firmware.isOtaSlotReady(context.capacity)) },
    earlierUpdatePending: { get: () => ['pending_reboot', 'pending_verify'].includes(context.capacity?.state) },
    fits: { get: () => context.selectedFile && context.capacity ? firmware.firmwareFitsSlot(context.selectedFile.size, context.capacity.maxFirmwareBytes) : null },
  });
  const page = loadPageFunctions(new URL('../components/system/OtaPanel.svelte', import.meta.url), [
    'captureOperation', 'loadCapacity', 'reset', 'onFileSelected', 'startUpload', 'waitForNode',
  ], context);
  return { c: context, page, changeNode: () => { context.generation++; context.nodeIp = '192.168.1.60'; context.nodeId = 'node-b'; } };
}

function systemWorkspace() {
  const context: Record<string, any> = { nodeIp: '192.168.1.50', nodeId: 'node-a', targetGeneration: 1,
    nodeSession: null, disposed: false, readRequest: 0, loadingRecovery: false,
    identity: { id: 'node-a' }, recoveryStatus: { safeMode: false }, showWizard: false, pageError: '',
    nodeAuthStore: { getSession: vi.fn(() => null) },
    getIdentity: vi.fn(async (ip: string) => ({ id: ip === '192.168.1.50' ? 'node-a' : 'node-b' })),
    getRecoveryStatus: vi.fn(async () => ({ safeMode: false })),
  };
  const page = loadPageFunctions(new URL('../../routes/dashboard/firmware/+page.svelte', import.meta.url), [
    'capturePageOperation', 'loadNodeState', 'syncFocusedNode',
  ], context);
  return { c: context, page };
}

afterEach(() => { vi.useRealTimers(); vi.restoreAllMocks(); });

describe('single-node OTA asynchronous ownership', () => {
  it('does not upload after an unavailable slot read', async () => {
    const { c, page } = otaWorkspace(); c.getVerifiedOtaStatus.mockRejectedValueOnce(new Error('offline'));
    await page.loadCapacity(); await page.startUpload();
    expect(c.capacity).toBeNull(); expect(c.capacityLoading).toBe(false);
    expect(c.error).toContain('offline'); expect(c.uploadFirmware).not.toHaveBeenCalled();
  });

  it('ignores an earlier node slot response without releasing a newer busy state', async () => {
    const { c, page, changeNode } = otaWorkspace(); const read = deferred(); c.getVerifiedOtaStatus.mockReturnValueOnce(read.promise);
    const pending = page.loadCapacity(); changeNode(); c.capacity = { ...slot, nodeId: 'node-b' }; c.capacityLoading = true;
    read.resolve(slot); await pending;
    expect(c.capacity.nodeId).toBe('node-b'); expect(c.capacityLoading).toBe(true);
  });

  it('does not start a write after the preflight owner changes', async () => {
    const { c, page, changeNode } = otaWorkspace(); const read = deferred(); c.getVerifiedOtaStatus.mockReturnValueOnce(read.promise);
    const pending = page.startUpload(); changeNode(); read.resolve(slot); await pending;
    expect(c.uploadFirmware).not.toHaveBeenCalled();
  });

  it('rechecks all slot metadata just before upload', async () => {
    const { c, page } = otaWorkspace(); c.getVerifiedOtaStatus.mockResolvedValueOnce({ ...slot, nextPartition: 'ota_0' });
    await page.startUpload(); expect(c.uploadFirmware).not.toHaveBeenCalled();
    expect(c.phase).toBe('select'); expect(c.error).toContain('not ready');
    expect(c.selectedFile.name).toBe('firmware.bin'); expect(c.capacity).toBeNull();
  });

  it('does not publish a late receipt or start polling after a node switch', async () => {
    const { c, page, changeNode } = otaWorkspace(); const upload = deferred(); c.uploadFirmware.mockReturnValueOnce(upload.promise);
    const pending = page.startUpload(); await Promise.resolve(); await Promise.resolve();
    expect(c.uploadFirmware).toHaveBeenCalledOnce();
    changeNode(); c.phase = 'select'; c.progress = 0; upload.resolve(receipt); await pending;
    expect(c.phase).toBe('select'); expect(c.uploadResult).toBeNull(); expect(c.progress).toBe(0);
    expect(c.getVerifiedOtaStatus).toHaveBeenCalledOnce();
  });

  it('does not publish a receipt after its exact authentication session changes', async () => {
    const { c, page } = otaWorkspace(); const upload = deferred(); c.uploadFirmware.mockReturnValueOnce(upload.promise);
    const pending = page.startUpload(); await Promise.resolve(); await Promise.resolve();
    c.nodeAuthStore.getSession.mockReturnValue({ sessionToken: 'replacement' });
    upload.resolve(receipt); await pending;
    expect(c.uploadResult).toBeNull(); expect(c.getVerifiedOtaStatus).toHaveBeenCalledOnce();
  });

  it('keeps an invalid accepted receipt unconfirmed instead of retrying automatically', async () => {
    const { c, page } = otaWorkspace(); c.uploadFirmware.mockRejectedValueOnce(new firmware.OtaUnconfirmedError('missing receipt'));
    await page.startUpload(); expect(c.phase).toBe('unconfirmed'); expect(c.error).toContain('missing receipt');
    expect(c.uploadFirmware).toHaveBeenCalledOnce();
  });

  it('waits for the accepted partition during a same-version update', async () => {
    vi.useFakeTimers(); const { c, page } = otaWorkspace();
    c.getVerifiedOtaStatus.mockResolvedValueOnce({ ...confirmed, runningPartition: 'ota_0', targetPartition: 'ota_0' }).mockResolvedValueOnce(confirmed);
    const pending = page.waitForNode(page.captureOperation(), target, new AbortController());
    await vi.advanceTimersByTimeAsync(3000); expect(c.phase).toBe('waiting');
    await vi.advanceTimersByTimeAsync(3000); await pending;
    expect(c.phase).toBe('success'); expect(vi.getTimerCount()).toBe(0);
  });

  it('reports a matching candidate rollback without treating the old version as success', async () => {
    vi.useFakeTimers(); const { c, page } = otaWorkspace();
    c.getVerifiedOtaStatus.mockResolvedValue({ ...confirmed, state: 'rolled_back', rolledBack: true, runningPartition: 'ota_0' });
    const pending = page.waitForNode(page.captureOperation(), target, new AbortController());
    await vi.advanceTimersByTimeAsync(3000); await pending; expect(c.phase).toBe('rolled_back');
  });

  it('ends a stalled verification at the real deadline and aborts its request', async () => {
    vi.useFakeTimers(); const { c, page } = otaWorkspace(); const operation = new AbortController();
    c.getVerifiedOtaStatus.mockImplementation((_ip: string, _id: string, signal: AbortSignal) => new Promise((_yes, no) => {
      signal.addEventListener('abort', () => no(new DOMException('cancelled', 'AbortError')), { once: true });
    }));
    const pending = page.waitForNode(page.captureOperation(), target, operation);
    await vi.advanceTimersByTimeAsync(90000); await pending;
    expect(c.phase).toBe('unconfirmed'); expect(operation.signal.aborted).toBe(true); expect(vi.getTimerCount()).toBe(0);
  });

  it('reset invalidates outstanding reads and keeps the newer capacity', async () => {
    const { c, page } = otaWorkspace(); const oldRead = deferred();
    c.getVerifiedOtaStatus.mockReturnValueOnce(oldRead.promise).mockResolvedValueOnce({ ...slot, maxFirmwareBytes: 3000000 });
    const pending = page.loadCapacity(); page.reset(); await Promise.resolve();
    oldRead.resolve(slot); await pending;
    expect(c.capacity.maxFirmwareBytes).toBe(3000000); expect(c.selectedFile).toBeNull();
  });

  it('rejecting a new file clears the old upload selection', () => {
    const { c, page } = otaWorkspace(); const input = { files: [new File([], 'empty.bin')], value: 'empty.bin' };
    page.onFileSelected({ target: input }); expect(c.selectedFile).toBeNull(); expect(input.value).toBe('');
  });
});

describe('System and Recovery page ownership', () => {
  it('clears a previous node identity and recovery view immediately on selection', async () => {
    const { c, page } = systemWorkspace(); const id = deferred(); c.getIdentity.mockReturnValueOnce(id.promise);
    c.showWizard = true; page.syncFocusedNode('192.168.1.60');
    expect(c.identity).toBeNull(); expect(c.recoveryStatus).toBeNull(); expect(c.nodeId).toBe(''); expect(c.showWizard).toBe(false);
    id.resolve({ id: 'node-b' }); await Promise.resolve(); await Promise.resolve();
    expect(c.nodeId).toBe('node-b');
  });

  it('never overwrites a later node identity with an earlier completed read', async () => {
    const { c, page } = systemWorkspace(); const id = deferred(); c.getIdentity.mockReturnValueOnce(id.promise);
    const pending = page.loadNodeState(c.nodeIp); page.syncFocusedNode('192.168.1.60');
    await Promise.resolve(); await Promise.resolve(); id.resolve({ id: 'node-a' }); await pending;
    expect(c.nodeId).toBe('node-b'); expect(c.identity.id).toBe('node-b');
  });

  it('keeps an identity read failure explicit and does not fabricate version metadata', async () => {
    const { c, page } = systemWorkspace(); c.identity = null; c.nodeId = ''; c.getIdentity.mockRejectedValue(new Error('offline'));
    await page.loadNodeState(c.nodeIp); expect(c.identity).toBeNull(); expect(c.nodeId).toBe('');
    expect(c.pageError).toContain('identity could not be loaded');
  });

  it('invalidates panels when a different node takes the same address', async () => {
    const { c, page } = systemWorkspace(); c.getIdentity.mockResolvedValue({ id: 'replacement' }); c.showWizard = true;
    const owner = page.capturePageOperation(); await page.loadNodeState(c.nodeIp);
    expect(owner.isCurrent()).toBe(false); expect(c.nodeId).toBe('replacement'); expect(c.recoveryStatus).toBeNull();
    expect(c.showWizard).toBe(false); expect(c.pageError).toContain('different node');
  });

  it('does not let an older refresh clear the latest status or loading indicator', async () => {
    const { c, page } = systemWorkspace(); const first = deferred(), second = deferred();
    c.getRecoveryStatus.mockReturnValueOnce(first.promise).mockReturnValueOnce(second.promise);
    const old = page.loadNodeState(c.nodeIp), latest = page.loadNodeState(c.nodeIp);
    first.reject(new Error('old failure')); await old;
    expect(c.pageError).toBe(''); expect(c.loadingRecovery).toBe(true);
    second.resolve({ safeMode: true }); await latest;
    expect(c.recoveryStatus.safeMode).toBe(true); expect(c.loadingRecovery).toBe(false);
  });

  it('rekeys the same selected address when its session changes', async () => {
    const { c, page } = systemWorkspace(); const owner = page.capturePageOperation();
    c.nodeAuthStore.getSession.mockReturnValue({ sessionToken: 'new' });
    page.syncFocusedNode(c.nodeIp); expect(owner.isCurrent()).toBe(false); expect(c.identity).toBeNull();
    await Promise.resolve(); await Promise.resolve();
  });
});
