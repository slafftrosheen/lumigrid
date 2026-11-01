import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { readFileSync } from 'node:fs';
import {
  expandChannelMask,
  channelsToMask,
  clampChannelValue,
  putManualState,
  releaseManualState,
  buildManualSnapshot,
  captureLiveDeskToScene,
  type ManualAddressableValue,
  type ManualPwmValue,
  type ManualChannelValue,
} from '$lib/api/manual';
import * as transport from '$lib/api/transport';
import { loadPageFunctions } from '$lib/testing/pageScriptHarness';
import { MAX_PRESETS, MAX_SCENE_NAME_BYTES, getUtf8ByteLength } from '$lib/api/scenes';

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


function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

function deviceConfig() {
  return { channels: Array.from({ length: 16 }, (_, index) => ({
    index, port: index < 8 ? `A${index + 1}` : `P${index - 7}`,
    kind: index < 8 ? 'addressable' : 'pwm', name: '', configured: true, enabled: true,
  })) };
}

function workspace() {
  let epoch = 0;
  let auth = {};
  const c: Record<string, any> = {
    nodeIp: 'node-a', workspaceGeneration: 1, configRequest: 0, disposed: false, workspaceOwner: null,
    workspaceSession: auth, config: null, loadingConfig: false, configLoaded: true, configError: '',
    selectedIndices: [0], controlledIndices: [0], multiSelect: false, channels: [],
    lastManualSnapshot: null, sendThrottleTimer: null, lastSendTimestamp: 0, toastTimeout: null, toast: '',
    sessionState: 'active', isManualActive: true, isSessionBusy: false, isConnected: true, sessionSending: false,
    sessionErrorMessage: null, controlledLabels: 'A1', acceptedValues: [{ index: 0, r: 100, g: 60, b: 20, brightness: 200 }],
    saveDialogOpen: false, savingScene: false, saveCapture: null, saveSceneError: '', sceneNameInput: '',
    MAX_PRESETS, MAX_SCENE_NAME_BYTES, getUtf8ByteLength, buildManualSnapshot, captureLiveDeskToScene,
    getDeviceConfig: vi.fn(async () => deviceConfig()), getPresets: vi.fn(async () => []),
    savePreset: vi.fn(async () => ({ saved: true, checksum: 42 })), showToast: vi.fn(),
    nodeAuthStore: { getSession: () => auth },
    session: { destroy: vi.fn(), cancel: vi.fn(), sendValues: vi.fn(async () => true),
      acquire: vi.fn(async () => true), release: vi.fn(async () => true), updateTargets: vi.fn(async () => true) },
    captureNodeOperation: (ip: string) => {
      const generation = epoch; const capturedAuth = auth;
      return { ip, isCurrent: () => ip === c.nodeIp && generation === epoch && auth === capturedAuth };
    },
  };
  const page = loadPageFunctions(new URL('./+page.svelte', import.meta.url), [
    'createDeskChannels', 'captureWorkspaceOperation', 'applyChannelConfiguration', 'loadConfig', 'syncFocusedNode',
    'clearPendingSend', 'sendManualSnapshot', 'commitImmediateSend', 'takeManualControl', 'releaseControl', 'updateTargets',
    'openSaveSceneDialog', 'handleConfirmSaveScene', 'buildControlledSnapshot', 'blackoutSelected',
  ], c);
  c.channels = page.createDeskChannels();
  page.applyChannelConfiguration(deviceConfig());
  c.workspaceOwner = page.captureWorkspaceOperation();
  return { c, page, changeNode: (ip = 'node-b') => { ++epoch; ++c.workspaceGeneration; c.nodeIp = ip; },
    reconnect: () => { ++epoch; }, changeSession: () => { auth = {}; } };
}

describe('Live Desk production page continuity', () => {
  afterEach(() => { vi.clearAllTimers(); vi.useRealTimers(); });

  it('maps all 16 physical ports by identity rather than response position and disables unconfigured outputs', () => {
    const { c, page } = workspace();
    const config = deviceConfig(); config.channels.reverse();
    config.channels.find(ch => ch.index === 0)!.name = 'Front wash';
    config.channels.find(ch => ch.index === 8)!.configured = false;
    c.channels[0].r = 17;
    page.applyChannelConfiguration(config);
    expect(c.channels[0]).toMatchObject({ index: 0, name: 'Front wash', kind: 'addressable', enabled: true, r: 17 });
    expect(c.channels[8]).toMatchObject({ index: 8, name: 'P1', kind: 'pwm', enabled: false });
    expect(c.channels[15]).toMatchObject({ index: 15, name: 'P8', enabled: true });
  });

  it('keeps saved configuration failures explicit instead of enabling all channels', async () => {
    const { c, page } = workspace();
    c.channels[0].r = 17; c.getDeviceConfig.mockRejectedValueOnce(new Error('Read unavailable'));
    await page.loadConfig();
    expect(c.configLoaded).toBe(false); expect(c.configError).toBe('Read unavailable');
    expect(c.channels.every((ch: any) => !ch.enabled)).toBe(true);
    expect(c.channels[0].r).toBe(17);
    await page.takeManualControl(); expect(c.session.acquire).not.toHaveBeenCalled();
  });

  it('rejects incomplete or contradictory physical mappings', async () => {
    const { c, page } = workspace();
    c.getDeviceConfig.mockResolvedValueOnce({ channels: deviceConfig().channels.slice(0, 8) });
    await page.loadConfig(); expect(c.configLoaded).toBe(false);
    const ambiguous = deviceConfig(); ambiguous.channels[8].port = 'A1';
    c.getDeviceConfig.mockResolvedValueOnce(ambiguous); await page.loadConfig();
    expect(c.configLoaded).toBe(false); expect(c.configError).toMatch(/ambiguous/);
  });

  it('does not publish a late configuration after A → B → A or clear a newer loading flag', async () => {
    const { c, page, changeNode } = workspace(); const pending = deferred<any>();
    c.getDeviceConfig.mockReturnValueOnce(pending.promise);
    const load = page.loadConfig(); changeNode(); changeNode('node-a');
    c.loadingConfig = true; c.configError = 'Current request';
    pending.resolve(deviceConfig()); await load;
    expect(c.configLoaded).toBe(false); expect(c.loadingConfig).toBe(true); expect(c.configError).toBe('Current request');
  });

  it('cancels an old lease and delayed send on reconnect while preserving same-session draft values', async () => {
    vi.useFakeTimers();
    const { c, page, reconnect } = workspace(); c.channels[0].r = 17;
    c.sendThrottleTimer = setTimeout(() => page.sendManualSnapshot(), 100);
    reconnect(); page.syncFocusedNode('node-a'); await Promise.resolve();
    expect(c.session.destroy).toHaveBeenCalledWith('node-a');
    expect(c.channels[0].r).toBe(17);
    await vi.advanceTimersByTimeAsync(100);
    expect(c.session.sendValues).not.toHaveBeenCalled();
  });

  it('clears a foreign-session draft and dialog even when the selected address is unchanged', () => {
    const { c, page, changeSession } = workspace(); c.channels[0].r = 17; page.openSaveSceneDialog();
    changeSession(); page.syncFocusedNode('node-a');
    expect(c.channels[0].r).toBe(255);
    expect(c.saveDialogOpen).toBe(false); expect(c.saveCapture).toBeNull();
    expect(c.session.destroy).toHaveBeenCalledWith('node-a');
  });

  it('freezes accepted values before the scene-library read and uses conditional creation', async () => {
    const { c, page } = workspace(); page.openSaveSceneDialog();
    const library = deferred<any[]>(); c.getPresets.mockReturnValueOnce(library.promise);
    const save = page.handleConfirmSaveScene('Captured look');
    c.acceptedValues[0].r = 240; c.channels[0].r = 240; c.controlledIndices = [8];
    library.resolve([]); await save;
    const [ip, submitted] = c.savePreset.mock.calls[0];
    expect(ip).toBe('node-a'); expect(submitted.expectedSceneChecksum).toBe(0);
    expect(submitted.channels[0].layers[0].primaryColor.r).toBe(100);
    expect(submitted.channels[8].layers[0].enabled).toBe(false);
    expect(c.showToast).toHaveBeenCalledWith('Saved as Scene 1: "Captured look"');
  });

  it('never saves an old capture after node or same-host credential replacement', async () => {
    for (const change of ['node', 'session']) {
      const { c, page, changeNode, changeSession } = workspace(); page.openSaveSceneDialog();
      const library = deferred<any[]>(); c.getPresets.mockReturnValueOnce(library.promise);
      const save = page.handleConfirmSaveScene('Captured look');
      if (change === 'node') changeNode(); else changeSession();
      c.savingScene = true; library.resolve([]); await save;
      expect(c.savePreset).not.toHaveBeenCalled(); expect(c.showToast).not.toHaveBeenCalled();
      expect(c.savingScene).toBe(true);
    }
  });

  it('retains capture errors and never reports an unacknowledged scene save as success', async () => {
    const { c, page } = workspace(); page.openSaveSceneDialog();
    c.savePreset.mockResolvedValueOnce({ saved: false }); await page.handleConfirmSaveScene('Captured look');
    expect(c.saveDialogOpen).toBe(true); expect(c.saveSceneError).toMatch(/did not confirm/);
    expect(c.showToast).not.toHaveBeenCalled(); expect(c.savingScene).toBe(false);
  });

  it('rejects an empty or oversized UTF-8 scene name before touching the node', async () => {
    const { c, page } = workspace(); page.openSaveSceneDialog();
    await page.handleConfirmSaveScene('  '); await page.handleConfirmSaveScene('é'.repeat(12));
    expect(c.getPresets).not.toHaveBeenCalled(); expect(c.saveSceneError).toMatch(/UTF-8 bytes/);
  });

  it('does not claim release success on rejection or after the source node changes', async () => {
    const { c, page, changeNode } = workspace(); c.session.release.mockResolvedValueOnce(false);
    await page.releaseControl(); expect(c.showToast).not.toHaveBeenCalled();
    const pending = deferred<boolean>(); c.session.release.mockReturnValueOnce(pending.promise);
    const release = page.releaseControl(); changeNode(); pending.resolve(true); await release;
    expect(c.showToast).not.toHaveBeenCalled();
  });

  it('keeps zeroing an uncontrolled selection draft-only and does not claim physical blackout', () => {
    const { c, page } = workspace(); c.selectedIndices = [8]; c.channels[8].pwm = 4095;
    page.blackoutSelected();
    expect(c.channels[8].pwm).toBe(0);
    expect(page.buildControlledSnapshot().map((ch: any) => ch.index)).toEqual([0]);
    expect(c.showToast).toHaveBeenCalledWith(expect.stringContaining('Other selections remain draft-only'));
  });
});
