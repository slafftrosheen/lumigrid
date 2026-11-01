import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { loadPageFunctions } from '$lib/testing/pageScriptHarness';

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(done => { resolve = done; });
  return { promise, resolve };
}

function workspace() {
  const session = {};
  const saved = { mode: 'sta_with_ap_fallback', ssid: 'Venue A', passwordSet: true, apName: 'Recovery A', apChannel: 6 };
  const runtime = { state: 'connected', applyState: 'idle', pendingCommandId: null, lastApplyError: null,
    persistedMatchesRuntime: true, staSsid: 'Venue A', staConnected: true };
  const c: Record<string, any> = {
    nodeIp: 'node-a', activeIp: 'node-a', activeSession: session, disposed: false,
    generation: 1, loadRequest: 0, statusRequest: 0, applyRequest: 0,
    loading: false, applying: false, configLoaded: true, isAdmin: true,
    config: saved, status: runtime, loadError: '', statusError: '',
    mode: saved.mode, ssid: saved.ssid, password: '', clearPassword: false, apName: saved.apName, apChannel: 6,
    validationError: null, applyError: null, applyMessage: null, pollTimer: null, deadlineTimer: null,
    nodeAuthStore: { getSession: () => session }, onNetworkChanged: vi.fn(),
    getNetworkConfig: vi.fn(async () => saved), getNetworkStatus: vi.fn(async () => runtime),
    saveNetworkConfig: vi.fn(async () => ({ accepted: true, commandId: 7, applyPending: true })),
  };
  Object.defineProperty(c, 'inputsDisabled', { get: () => c.loading || c.applying || !c.configLoaded || !c.isAdmin });
  const page = loadPageFunctions(new URL('./NetworkConfigCard.svelte', import.meta.url), [
    'restoreDraft', 'savedDraft', 'captureOwner', 'stopPolling', 'loadData',
    'validateInput', 'handleApply', 'matchesSaved', 'startReconnectPolling',
  ], c);
  return { c, page, saved, runtime };
}

describe('NetworkConfigCard production async workflows', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => { vi.clearAllTimers(); vi.restoreAllMocks(); vi.useRealTimers(); });

  it('does not save default inputs until this node’s saved settings loaded', async () => {
    const { c, page } = workspace();
    c.configLoaded = false;
    await page.handleApply();
    expect(c.saveNetworkConfig).not.toHaveBeenCalled();
  });

  it('preserves a saved password when blank, and clears it only on the explicit checkbox', async () => {
    const first = workspace();
    await first.page.handleApply();
    expect(first.c.saveNetworkConfig.mock.calls[0][1]).not.toHaveProperty('password');
    expect(first.c.saveNetworkConfig.mock.calls[0][1]).not.toHaveProperty('clearPassword');
    const second = workspace();
    second.c.clearPassword = true;
    await second.page.handleApply();
    expect(second.c.saveNetworkConfig.mock.calls[0][1]).toHaveProperty('clearPassword', true);
  });

  it('rejects fractional, empty and out-of-range AP channels before any save', async () => {
    const { c, page } = workspace();
    for (const channel of [undefined, 0, 1.5, 14]) {
      c.apChannel = channel;
      await page.handleApply();
      expect(c.validationError).toContain('whole number');
    }
    expect(c.saveNetworkConfig).not.toHaveBeenCalled();
  });

  it('keeps a failed config read unavailable even when runtime status succeeds', async () => {
    const { c, page } = workspace();
    c.getNetworkConfig.mockRejectedValueOnce(new Error('offline'));
    await page.loadData();
    expect(c.configLoaded).toBe(false);
    expect(c.loadError).toContain('unavailable');
    await page.handleApply();
    expect(c.saveNetworkConfig).not.toHaveBeenCalled();
  });

  it('ignores config and status from an earlier node selection', async () => {
    const { c, page, saved } = workspace();
    const pending = deferred<any>();
    c.getNetworkConfig.mockReturnValueOnce(pending.promise);
    const load = page.loadData();
    c.nodeIp = c.activeIp = 'node-b'; c.generation++;
    c.ssid = 'Node B draft'; c.config = null;
    pending.resolve(saved);
    await load;
    expect(c.ssid).toBe('Node B draft');
    expect(c.config).toBeNull();
  });

  it('keeps denied or unconfirmed saves as failures without starting confirmation polls', async () => {
    const { c, page } = workspace();
    c.ssid = 'My preserved draft'; c.password = 'NewPassword123';
    c.saveNetworkConfig.mockRejectedValueOnce(new Error('HTTP 403 not permitted'));
    await page.handleApply();
    expect(c.applyError).toContain('not confirmed');
    expect(c.ssid).toBe('My preserved draft');
    expect(c.password).toBe('NewPassword123');
    await vi.advanceTimersByTimeAsync(65000);
    expect(c.getNetworkStatus).not.toHaveBeenCalled();
  });

  it('does not let a poll from a timed-out apply finish the next apply on the same node', async () => {
    const { c, page, runtime } = workspace();
    c.ssid = 'Attempt A';
    c.getNetworkStatus.mockResolvedValue({ ...runtime, applyState: 'waiting_for_network', pendingCommandId: 7 });
    await page.handleApply();
    await vi.advanceTimersByTimeAsync(56000);
    const lateStatus = deferred<any>();
    c.getNetworkStatus.mockReturnValueOnce(lateStatus.promise);
    await vi.advanceTimersByTimeAsync(2000); // A poll begins at t=58 s, inside its 8 s API window.
    await vi.advanceTimersByTimeAsync(3000); // A's 60 s operation deadline expires.
    expect(c.applying).toBe(false);
    expect(c.applyError).toContain('after 60 seconds');

    c.ssid = 'Attempt B';
    c.saveNetworkConfig.mockResolvedValueOnce({ accepted: true, commandId: 8, applyPending: true });
    c.getNetworkStatus.mockResolvedValue({ ...runtime, applyState: 'waiting_for_network', pendingCommandId: 8 });
    await page.handleApply();
    lateStatus.resolve(runtime);
    await Promise.resolve(); await Promise.resolve();
    expect(c.getNetworkConfig).not.toHaveBeenCalled();
    expect(c.applying).toBe(true);
    expect(c.ssid).toBe('Attempt B');
    expect(c.onNetworkChanged).not.toHaveBeenCalled();
    const count = c.getNetworkStatus.mock.calls.length;
    await vi.advanceTimersByTimeAsync(2000);
    expect(c.getNetworkStatus.mock.calls.length).toBeGreaterThan(count);
    expect(c.applying).toBe(true);
  });
});
