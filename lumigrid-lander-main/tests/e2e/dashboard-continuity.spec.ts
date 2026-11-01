import { test, expect, type Page, type Route } from '@playwright/test';
import { createDefaultPreset } from '../../src/lib/api/scenes';
import { createDefaultSceneMotion } from '../../src/lib/api/sceneMotion';

const a = '192.168.1.50';
const b = '192.168.1.60';
const headers = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'Authorization, Content-Type, X-API-Key',
  'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
};
const config = (ip: string) => ({ mode: 'sta_with_ap_fallback', ssid: `Venue-${ip === a ? 'A' : 'B'}`,
  passwordSet: true, apName: `LumiGrid-${ip === a ? 'A' : 'B'}`, apChannel: 6 });
const network = (ip: string) => ({ state: 'connected', mode: 'sta_with_ap_fallback', staConnected: true,
  staSsid: config(ip).ssid, staRssi: -52, staIp: ip, apActive: false, apIp: '', retryCount: 0,
  applyState: 'idle', pendingCommandId: null, lastApplyError: null, lastDisconnectReason: 0,
  persistedMatchesRuntime: true });
const runtime = (ip: string) => ({
  schema: 3, streamSeq: 1, generatedAtMs: 12000, node: { id: `node-${ip}`, bootSessionId: 1 },
  runtime: { effect: 'solid', brightness: 160, testActive: false, autoLoop: false, controlOwner: 'ScenePlayback', frameCount: 400 },
  output: { enabled: true, powerMw: 1200, state: 'healthy', fault: false, faultCount: 0, estopActive: false,
    power: { estimatedRequestedMw: 1200, estimatedLimitedMw: 1200, estimateComplete: true, limitMw: 10000 } },
  scene: { activePreset: 0, requestedPreset: 0, requestedGeneration: 1, appliedGeneration: 1, applyPending: false, transitioning: false, fallbackActive: false },
  manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
  transport: { mode: 'scene', generation: 1, sceneIndex: 0, timeline: { running: false, bpm: 120 }, cue: { running: false } },
  sync: { configuredRole: 'auto', activeRole: 'leader', clockLocked: false },
  network: { state: 'connected', rssi: -52, staConnected: true, staIp: ip, apActive: false, applyState: 'idle' },
  protocols: { artnet: { enabled: true, sources: 0, packetsPerSecond: 0 }, sacn: { enabled: false, sources: 0, packetsPerSecond: 0 } },
  timeline: { clockMs: 12000, driftPpm: 0, synced: false }, automation: { activeTracks: 0, transitionCurve: 'linear' }, job: null, channels: [],
});
const health = {
  schema_version: 1,
  system: { uptime_s: 100, free_heap: 50000, min_free_heap: 40000, reset_reason: 1, fps: 60 },
  network: { wifi_rssi: -52 }, nvs: { used: 10, free: 90 },
  config: { valid: true, error_count: 0, first_error: '', persistence_state: 'saved', primary_valid: true, backup_valid: true },
  startup: { state: 'ok', stage: 'app', failing_stage: '', failing_error: 0 },
  output: { state: 'healthy', fault: false, current_power_mw: 1200 },
  dmx: { sources_active: 0, packets_accepted: 0 }, sync: { packet_loss: 0 },
};

type Override = (route: Route, ip: string, path: string) => Promise<boolean>;
async function fixture(page: Page, override?: Override, authenticated = true) {
  const calls: { ip: string; path: string; method: string; body: any }[] = [];
  const streams: { ip: string; send: (message: string | Buffer) => void }[] = [];
  await page.addInitScript(({ a, b, authenticated }) => {
    localStorage.setItem('lg_node_ip', a);
    localStorage.setItem('lg_nodes', JSON.stringify([a, b]));
    if (authenticated) sessionStorage.setItem('lg_node_sessions', JSON.stringify([a, b].map(ip => ({
      address: ip, nodeId: `node-${ip}`, role: 'admin', sessionToken: `session-${ip}`, expiresAt: Date.now() + 3600000,
    }))));
  }, { a, b, authenticated });
  for (const ip of [a, b]) {
    await page.route(`http://${ip}/**`, async route => {
      const request = route.request();
      const path = new URL(request.url()).pathname;
      calls.push({ ip, path, method: request.method(), body: request.postData() ? request.postDataJSON() : null });
      if (override && await override(route, ip, path)) return;
      let response: any = {};
      if (path === '/api/v1/auth/status') response = { provisioned: true, bootstrapAllowed: false };
      if (path === '/api/v1/auth/session') response = { authenticated: true, role: 'admin', sessionToken: `session-${ip}`, expiresIn: 1800 };
      if (path === '/api/v1/auth/ws-ticket') response = { ticket: 'b'.repeat(32), expiresInMs: 15000 };
      if (path === '/api/v1/status') response = runtime(ip);
      if (path === '/api/v1/health') response = health;
      if (path === '/api/v1/info') response = { id: `node-${ip}`, name: `Node ${ip}`, firmwareVersion: 'test-build' };
      if (path === '/api/v1/config') response = { deviceName: `Node-${ip}`, limits: {}, physicalOutputs: [], fixtures: [] };
      if (path === '/api/v1/network/config') response = config(ip);
      if (path === '/api/v1/network/status') response = network(ip);
      await route.fulfill({ json: response, headers });
    });
    await page.routeWebSocket(`ws://${ip}/ws`, socket => {
      streams.push({ ip, send: message => socket.send(message) });
      socket.send(JSON.stringify(runtime(ip)));
    });
  }
  return { calls, streams };
}

test('saved network settings must load before writes are enabled', async ({ page }) => {
  const { calls } = await fixture(page, async (route, _ip, path) => {
    if (path !== '/api/v1/network/config') return false;
    await route.fulfill({ status: 503, json: { error: 'unavailable' }, headers });
    return true;
  });
  await page.goto('/dashboard');
  await expect(page.locator('.network-card')).toContainText('Saved network settings are unavailable');
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toBeDisabled();
  await expect(page.getByRole('button', { name: 'Apply Network Settings' })).toBeDisabled();
  expect(calls.filter(c => c.path === '/api/v1/network/config' && c.method === 'PUT')).toHaveLength(0);
});

test('late network reads cannot populate another selected node', async ({ page }) => {
  let release!: () => void;
  const waiting = new Promise<void>(resolve => { release = resolve; });
  let requested!: () => void;
  const started = new Promise<void>(resolve => { requested = resolve; });
  await fixture(page, async (route, ip, path) => {
    if (ip !== a || path !== '/api/v1/network/config') return false;
    requested();
    await waiting;
    await route.fulfill({ json: config(a), headers });
    return true;
  });
  await page.goto('/dashboard');
  await started;
  await page.locator('.node-select').selectOption(b);
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toHaveValue('Venue-B');
  release();
  await page.waitForResponse(`http://${a}/api/v1/network/config`);
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toHaveValue('Venue-B');
  await expect(page.locator('.network-card')).toContainText(b);
});

test('denied network save preserves the draft and never becomes a successful poll', async ({ page }) => {
  const { calls } = await fixture(page, async (route, _ip, path) => {
    if (path !== '/api/v1/network/config' || route.request().method() !== 'PUT') return false;
    await route.fulfill({ status: 403, json: { error: 'not permitted' }, headers });
    return true;
  });
  await page.goto('/dashboard');
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toBeEnabled();
  await page.getByLabel('Wi-Fi Network SSID', { exact: true }).fill('My draft network');
  await page.getByLabel('Wi-Fi Password', { exact: true }).fill('DraftPassword123');
  await page.getByRole('button', { name: 'Apply Network Settings' }).click();
  await expect(page.locator('.network-card .error-banner')).toContainText('not confirmed');
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toHaveValue('My draft network');
  await expect(page.getByLabel('Wi-Fi Password', { exact: true })).toHaveValue('DraftPassword123');
  expect(calls.filter(c => c.path === '/api/v1/network/status')).toHaveLength(1);
});

test('network confirmation deadline also ends a perpetually transitioning node', async ({ page }) => {
  await page.clock.install();
  let applying = false;
  await fixture(page, async (route, ip, path) => {
    if (path === '/api/v1/network/config' && route.request().method() === 'PUT') {
      applying = true;
      await route.fulfill({ json: { accepted: true, commandId: 7, applyPending: true }, headers });
      return true;
    }
    if (path === '/api/v1/network/status' && applying) {
      await route.fulfill({ json: { ...network(ip), applyState: 'waiting_for_network', pendingCommandId: 7 }, headers });
      return true;
    }
    return false;
  });
  await page.goto('/dashboard');
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toBeEnabled();
  await page.getByLabel('Wi-Fi Network SSID', { exact: true }).fill('Preserve through timeout');
  await page.getByRole('button', { name: 'Apply Network Settings' }).click();
  await expect(page.locator('.network-card .applying-banner')).toContainText('Settings accepted');
  await page.clock.fastForward(61000);
  await expect(page.locator('.network-card .error-banner')).toContainText('after 60 seconds');
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toHaveValue('Preserve through timeout');
  await expect(page.getByRole('button', { name: 'Apply Network Settings' })).toBeEnabled();
});

test('late network save cannot start confirmation against the next node', async ({ page }) => {
  let release!: () => void;
  const waiting = new Promise<void>(resolve => { release = resolve; });
  const { calls } = await fixture(page, async (route, ip, path) => {
    if (ip !== a || path !== '/api/v1/network/config' || route.request().method() !== 'PUT') return false;
    await waiting;
    await route.fulfill({ json: { accepted: true, commandId: 7, applyPending: true }, headers });
    return true;
  });
  await page.goto('/dashboard');
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toBeEnabled();
  await page.getByLabel('Wi-Fi Network SSID', { exact: true }).fill('Only for A');
  await page.getByRole('button', { name: 'Apply Network Settings' }).click();
  await page.locator('.node-select').selectOption(b);
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toHaveValue('Venue-B');
  const response = page.waitForResponse(res => res.url() === `http://${a}/api/v1/network/config` && res.request().method() === 'PUT');
  release();
  await response;
  await expect(page.locator('.network-card .applying-banner')).toHaveCount(0);
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toHaveValue('Venue-B');
  expect(calls.filter(c => c.ip === b && c.path === '/api/v1/network/status')).toHaveLength(1);
  await page.locator('.node-select').selectOption(a);
  await expect(page.getByLabel('Wi-Fi Network SSID', { exact: true })).toHaveValue('Only for A');
});

test('late login cannot install its token on the new selected node', async ({ page }) => {
  let release!: () => void;
  const waiting = new Promise<void>(resolve => { release = resolve; });
  await fixture(page, async (route, ip, path) => {
    if (ip !== a || path !== '/api/v1/auth/session' || route.request().method() !== 'POST') return false;
    await waiting;
    await route.fulfill({ json: { authenticated: true, role: 'admin', sessionToken: 'obsolete-a', expiresIn: 1800 }, headers });
    return true;
  }, false);
  await page.goto('/dashboard');
  await page.getByPlaceholder('Node Access Key', { exact: true }).fill('AccessKeyForNodeA');
  await page.getByRole('button', { name: 'Sign In', exact: true }).click();
  await page.getByRole('button', { name: 'Switch Node', exact: true }).click();
  await page.locator('.pin-node-select').selectOption(b);
  await page.getByPlaceholder('Node Access Key', { exact: true }).fill('AccessKeyForNodeB');
  await page.getByRole('button', { name: 'Sign In', exact: true }).click();
  await expect(page.locator('.node-select')).toHaveValue(b);
  const response = page.waitForResponse(`http://${a}/api/v1/auth/session`);
  release();
  await response;
  await expect(page.locator('.node-select')).toHaveValue(b);
  const sessions = await page.evaluate(() => JSON.parse(sessionStorage.getItem('lg_node_sessions') || '[]'));
  expect(sessions.find((s: any) => s.address === b)?.sessionToken).toBe(`session-${b}`);
  expect(sessions.some((s: any) => s.sessionToken === 'obsolete-a')).toBe(false);
});

test('preview traffic cannot conceal stale status; overview remains usable on mobile', async ({ page }, testInfo) => {
  await page.clock.install();
  const { streams } = await fixture(page);
  await page.setViewportSize({ width: 1440, height: 1000 });
  await page.goto('/dashboard');
  await expect(page.locator('.status-badge')).toHaveText('LIVE');
  await expect(page.locator('.status-indicator .status-text')).toHaveText('Live');
  expect(await page.locator('.node-ip-heading').evaluate(el => getComputedStyle(el).webkitTextFillColor)).toBe('rgb(245, 245, 247)');
  await expect(page.locator('.health-section')).toContainText('Estimated power: 1.2 W');
  const incomplete = runtime(a);
  incomplete.streamSeq = 2;
  incomplete.output.power.estimateComplete = false;
  incomplete.output.power.limitMw = 500;
  streams.at(-1)!.send(JSON.stringify(incomplete));
  await expect(page.locator('.health-section')).toContainText('Incomplete estimate');
  await page.screenshot({ path: testInfo.outputPath('dashboard-desktop.png'), fullPage: true, animations: 'disabled' });
  await page.clock.fastForward(11000);
  streams.at(-1)!.send(Buffer.from([2, 1, 1, 0, 255, 45, 85]));
  await expect(page.locator('.status-badge')).toHaveText('STATUS STALE');
  await expect(page.locator('.status-indicator .status-text')).toHaveText('Stale');
  await expect(page.getByLabel('Brightness', { exact: true })).toBeDisabled();
  await expect(page.locator('.node-header')).toContainText('Status is stale');
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
  expect(await page.locator('.mode-selector').evaluate(el => {
    const bounds = el.getBoundingClientRect();
    return [...el.querySelectorAll('.mode-btn > span:first-child')].every(label => {
      const box = label.getBoundingClientRect();
      return box.left >= bounds.left && box.right <= bounds.right;
    });
  })).toBe(true);
  await page.screenshot({ path: testInfo.outputPath('dashboard-mobile-stale.png'), fullPage: true, animations: 'disabled' });
});

test('brightness confirmation preserves a newer slider draft', async ({ page }) => {
  const { calls, streams } = await fixture(page, async (route, _ip, path) => {
    if (path !== '/api/v1/factory/effect') return false;
    await route.fulfill({ json: { accepted: true }, headers });
    return true;
  });
  await page.goto('/dashboard');
  const slider = page.getByLabel('Brightness', { exact: true });
  await expect(slider).toBeEnabled();
  await slider.evaluate((input: HTMLInputElement) => {
    input.value = '100';
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await expect(page.locator('.brightness-item')).toContainText('Awaiting confirmation');
  await expect(slider).toBeEnabled();
  await slider.evaluate((input: HTMLInputElement) => {
    input.value = '220';
    input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  const confirmed = runtime(a);
  confirmed.streamSeq = 2;
  confirmed.runtime.brightness = 100;
  streams.at(-1)!.send(JSON.stringify(confirmed));
  await expect(page.locator('.brightness-item')).toContainText('Draft preserved · Node: 100');
  await expect(slider).toHaveValue('220');
  expect(calls.filter(c => c.path === '/api/v1/factory/effect')).toHaveLength(1);
});

async function sceneFixture(page: Page, options: { references?: Promise<void>; conflict?: boolean } = {}) {
  const presets = [
    { ...createDefaultPreset(0, 'Warm Wash'), checksum: 11 },
    { ...createDefaultPreset(1, 'Evening Glow'), checksum: 21 },
  ];
  let freshReads = 0;
  const result = await fixture(page, async (route, _ip, path) => {
    let response: unknown;
    if (path === '/api/v1/presets') {
      if (route.request().method() === 'PUT' && options.conflict) {
        await route.fulfill({ status: 409, json: { error: 'scene_conflict' }, headers });
        return true;
      }
      const index = new URL(route.request().url()).searchParams.get('index');
      if (index !== null) {
        freshReads++;
        response = { ...presets[Number(index)], name: 'Updated on Node', checksum: 31 };
      } else response = presets;
    } else if (path === '/api/v1/scene-motion') {
      const index = Number(new URL(route.request().url()).searchParams.get('index') || 0);
      response = { ...createDefaultSceneMotion(index), checksum: 12 + freshReads };
    } else if (path === '/api/v1/effects') response = [{ id: 'solid', label: 'Solid' }];
    else if (path === '/api/v1/palettes') response = [{ id: 0, label: 'Scene Colors' }];
    else if (path === '/api/v1/channels') response = { channels: [] };
    else if (path === '/api/v1/presets/references') {
      if (options.references) await options.references;
      response = { sceneIndex: 0, active: false, fallback: false, cues: [], timelineTracks: [], schedules: [] };
    } else return false;
    await route.fulfill({ json: response, headers });
    return true;
  });
  return { ...result, freshReads: () => freshReads };
}

test('Scene Studio dialogs open, retain keyboard focus, and return focus to their trigger', async ({ page }) => {
  let releaseReferences!: () => void;
  const references = new Promise<void>(resolve => { releaseReferences = resolve; });
  await sceneFixture(page, { references });
  await page.goto('/dashboard/scenes');
  const name = page.getByLabel('Scene Name', { exact: true });
  await expect(name).toHaveValue('Warm Wash');

  const create = page.getByRole('button', { name: '+ New Scene', exact: true });
  await create.click();
  const newDialog = page.getByRole('dialog', { name: 'Create New Scene', exact: true });
  await expect(newDialog).toBeVisible();
  await expect(newDialog.getByRole('button', { name: 'Cancel', exact: true })).toBeFocused();
  await newDialog.getByRole('button', { name: 'Create Scene', exact: true }).focus();
  await page.keyboard.press('Tab');
  await expect(newDialog.getByRole('button', { name: '03', exact: true })).toBeFocused();
  await page.keyboard.press('Shift+Tab');
  await expect(newDialog.getByRole('button', { name: 'Create Scene', exact: true })).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(newDialog).toHaveCount(0);
  await expect(create).toBeFocused();

  await name.fill('Local draft');
  await page.locator('.scene-card').filter({ hasText: 'Evening Glow' }).click();
  const unsaved = page.getByRole('dialog', { name: 'Unsaved Scene Changes', exact: true });
  await expect(unsaved).toBeVisible();
  await expect(unsaved.getByRole('button', { name: 'Stay on Scene' })).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(unsaved).toHaveCount(0);
  await expect(name).toHaveValue('Local draft');
  await name.fill('Warm Wash');

  const remove = page.getByRole('button', { name: 'Delete', exact: true });
  await remove.click();
  const deletion = page.getByRole('dialog', { name: 'Delete Scene 01?', exact: true });
  await expect(deletion).toContainText('Evaluating scene dependencies');
  await expect(deletion.getByRole('button', { name: 'Cancel', exact: true })).toBeFocused();
  releaseReferences();
  await expect(deletion.getByRole('button', { name: 'Delete Scene', exact: true })).toBeVisible();
  await expect(deletion.getByRole('button', { name: 'Cancel', exact: true })).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(deletion).toHaveCount(0);
  await expect(remove).toBeFocused();
});

test('Scene Studio conflict reload requests the current node document', async ({ page }) => {
  const { calls, freshReads } = await sceneFixture(page, { conflict: true });
  await page.goto('/dashboard/scenes');
  const name = page.getByLabel('Scene Name', { exact: true });
  await expect(name).toHaveValue('Warm Wash');
  await name.fill('Local draft');
  await page.getByRole('button', { name: 'Save', exact: true }).click();
  await expect(page.locator('.conflict-banner')).toContainText('This scene changed on the node');
  await expect(name).toHaveValue('Local draft');
  await page.getByRole('button', { name: 'Reload Node Version', exact: true }).click();
  await expect(name).toHaveValue('Updated on Node');
  expect(freshReads()).toBe(1);
  expect(calls.some(call => call.path.includes('/activate'))).toBe(false);
});
