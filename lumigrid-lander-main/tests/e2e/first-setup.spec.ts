import { test, expect, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { gunzipSync } from 'node:zlib';

const key = 'MyChosenAdminAccessKey2026!';
const session = 'a'.repeat(64);
// Same canonical contract returned by GET /status and WebSocket on the node.
const runtimeStatus = {
  schema: 3, streamSeq: 1, generatedAtMs: 12000, node: { id: 'test-node', bootSessionId: 1 },
  runtime: { effect: 'solid', brightness: 255, testActive: false, autoLoop: false, controlOwner: 'ScenePlayback', frameCount: 400 },
  output: { enabled: true, powerMw: 1200, state: 'healthy', fault: false, faultCount: 0, estopActive: false,
    power: { estimatedRequestedMw: 1200, estimatedLimitedMw: 1200, estimateComplete: true, limitMw: 10000 } },
  scene: { activePreset: 0, requestedPreset: 0, requestedGeneration: 1, appliedGeneration: 1, applyPending: false, transitioning: false, fallbackActive: false },
  manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
  transport: { mode: 'scene', generation: 1, sceneIndex: 0, timeline: { running: false, bpm: 120 }, cue: { running: false } },
  sync: { configuredRole: 'auto', activeRole: 'leader', clockLocked: false },
  network: { state: 'connected', rssi: -55, staConnected: true, staIp: '192.168.4.1', apActive: false, applyState: 'idle' },
  protocols: { artnet: { enabled: true, sources: 0, packetsPerSecond: 0 }, sacn: { enabled: false, sources: 0, packetsPerSecond: 0 } },
  timeline: { clockMs: 12000, driftPpm: 0, synced: false }, automation: { activeTracks: 0, transitionCurve: 'linear' }, job: null, channels: []
};
test.use({ launchOptions: { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH } });

async function nodeFixture(page: Page, provisioned = false, allowed = true) {
  const calls: { path: string; body: any; headers: Record<string, string> }[] = [];
  const credentials: Record<string, boolean> = { adminConfigured: true, installerConfigured: false, operatorConfigured: false };
  await page.route('http://192.168.4.1/**', async route => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    const body = request.postData() ? request.postDataJSON() : undefined;
    calls.push({ path, body, headers: request.headers() });
    if (path === '/') {
      const asset = readFileSync(resolve('../LumigridLedNode/generated/dashboard_assets.hpp'), 'utf8');
      const bytes = asset.match(/0x[0-9a-f]{2}/g)!.map(value => parseInt(value, 16));
      return route.fulfill({ contentType: 'text/html', body: gunzipSync(Buffer.from(bytes)).toString('utf8') });
    }
    let response: any = {};
    if (path === '/api/v1/auth/status') response = { provisioned, bootstrapAllowed: !provisioned && allowed };
    if (path === '/api/v1/auth/session') response = { authenticated: true, role: 'admin', sessionToken: session, expiresIn: 1800, setupRequired: !provisioned };
    if (path === '/api/v1/status') response = runtimeStatus;
    if (path === '/api/v1/auth/bootstrap') {
      expect(body.adminToken).toBe(key);
      provisioned = true;
      response = { provisioned: true, authenticated: true, role: 'admin', sessionToken: session, expiresIn: 1800 };
    }
    if (['/api/v1/presets', '/api/v1/effects', '/api/v1/palettes', '/api/v1/schedules', '/api/v1/cue-lists'].includes(path)) response = [];
    if (path === '/api/v1/channels') response = { channels: [] };
    if (path === '/api/v1/config') response = { deviceName: 'LumiGrid', limits: {}, physicalOutputs: [], fixtures: [] };
    if (path === '/api/v1/auth/ws-ticket') response = { ticket: 'b'.repeat(32), expiresInMs: 15000 };
    if (path === '/api/v1/auth/credentials') {
      if (body?.role) credentials[body.role + 'Configured'] = request.method() !== 'DELETE';
      response = credentials;
    }
    await route.fulfill({ json: response, headers: { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': 'Authorization, Content-Type, X-API-Key', 'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS' } });
  });
  return calls;
}

test('embedded first setup: visible defaults, confirmation, direct Admin session (K-N, P-Q)', async ({ page }) => {
  const calls = await nodeFixture(page);
  await page.goto('http://192.168.4.1');
  await expect(page.locator('#first-setup')).toBeVisible();
  await expect(page.locator('#normal-login')).toBeHidden();
  await expect(page.locator('#setup-defaults')).toContainText('LumiGrid-Setup');
  await expect(page.locator('#setup-defaults')).toContainText('192.168.4.1');
  await expect(page.locator('#first-setup')).not.toContainText(/MAC|bootstrap/i);
  await page.locator('#setup-admin').fill(key);
  await page.locator('#setup-confirm').fill('DifferentAdminAccessKey');
  await page.locator('#setup-continue').click();
  await expect(page.locator('#setup-error')).toContainText('confirm');
  expect(calls.filter(c => c.path.endsWith('/bootstrap'))).toHaveLength(0);
  await page.locator('#setup-confirm').fill(key);
  await page.locator('#setup-continue').click();
  await expect(page.locator('#setup-wizard')).toBeVisible();
  await expect(page.locator('#experience-auth')).toBeHidden();
  expect(await page.evaluate(() => JSON.parse(sessionStorage.getItem('lg_node_session') || 'null')?.token)).toBe(session);
  expect(calls.filter(c => c.path.endsWith('/session') && c.body?.token === 'lumigrid123')).toHaveLength(0);
  expect(calls.filter(c => c.path.endsWith('/bootstrap'))).toHaveLength(1);
  expect(await page.locator('#setup-admin').inputValue()).toBe('');
});

test('embedded provisioned node shows ordinary access-key login (O)', async ({ page }) => {
  await nodeFixture(page, true);
  await page.goto('http://192.168.4.1');
  await expect(page.locator('#normal-login')).toBeVisible();
  await expect(page.locator('#first-setup')).toBeHidden();
});

test('embedded recovery does not expose setup defaults', async ({ page }) => {
  await nodeFixture(page, false, false);
  await page.goto('http://192.168.4.1');
  await expect(page.locator('#setup-unavailable')).toBeVisible();
  await expect(page.locator('#first-setup')).toBeHidden();
});

async function openExternal(page: Page) {
  await page.addInitScript(() => {
    localStorage.setItem('lg_node_ip', '192.168.4.1');
    localStorage.setItem('lg_nodes', JSON.stringify(['192.168.4.1']));
  });
  await page.goto('/dashboard');
}

test('Svelte first setup asks for a new confirmed key and enters the wizard (K-N, P-Q)', async ({ page }) => {
  const calls = await nodeFixture(page);
  await openExternal(page);
  await expect(page.getByText('Secure your LumiGrid')).toBeVisible();
  await expect(page.getByPlaceholder('Node Access Key', { exact: true })).toHaveCount(0);
  await expect(page.locator('.pin-card')).toContainText('LumiGrid-Setup');
  await expect(page.locator('.pin-card')).toContainText('192.168.4.1');
  await expect(page.locator('.pin-card')).not.toContainText(/MAC|bootstrap/i);
  await page.getByLabel('New Admin Access Key', { exact: true }).fill(key);
  await page.getByLabel('Confirm Admin Access Key', { exact: true }).fill('DifferentAdminAccessKey');
  await expect(page.getByRole('button', { name: 'Continue Setup' })).toBeDisabled();
  await page.getByLabel('Confirm Admin Access Key', { exact: true }).fill(key);
  await page.getByRole('button', { name: 'Continue Setup' }).click();
  await expect(page.getByRole('heading', { name: 'Setup Wizard' })).toBeVisible();
  const requests = calls.filter(c => c.path.endsWith('/bootstrap'));
  expect(requests).toHaveLength(1);
  expect(requests[0].headers['x-api-key']).toBeUndefined();
  expect(calls.filter(c => c.path.endsWith('/session') && c.body?.token === key)).toHaveLength(0);
});

test('Svelte provisioned node shows normal access-key login (O)', async ({ page }) => {
  await nodeFixture(page, true);
  await openExternal(page);
  await expect(page.getByPlaceholder('Node Access Key', { exact: true })).toBeVisible();
  await expect(page.getByText('Secure your LumiGrid')).toHaveCount(0);
});

test('Svelte provisioned node login connects nodeStore and renders dashboard (LG-038.7 §35)', async ({ page }) => {
  const calls = await nodeFixture(page, true);
  await openExternal(page);
  await expect(page.getByPlaceholder('Node Access Key', { exact: true })).toBeVisible();
  await page.getByPlaceholder('Node Access Key', { exact: true }).fill(key);
  await page.getByRole('button', { name: 'Sign In' }).click();
  await expect(page.getByPlaceholder('Node Access Key', { exact: true })).toHaveCount(0);
  expect(calls.some(c => c.path.endsWith('/session'))).toBe(true);
});

test('embedded startup reveals dashboard shell immediately after status before secondary calls finish (LG-038.7 §36)', async ({ page }) => {
  let delaySecondary = true;
  await page.route('http://192.168.4.1/**', async route => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    if (path === '/') {
      const asset = readFileSync(resolve('../LumigridLedNode/generated/dashboard_assets.hpp'), 'utf8');
      const bytes = asset.match(/0x[0-9a-f]{2}/g)!.map(value => parseInt(value, 16));
      return route.fulfill({ contentType: 'text/html', body: gunzipSync(Buffer.from(bytes)).toString('utf8') });
    }
    if (path === '/api/v1/auth/status') {
      return route.fulfill({ json: { provisioned: true, bootstrapAllowed: false } });
    }
    if (path === '/api/v1/auth/session') {
      return route.fulfill({ json: { authenticated: true, role: 'admin', sessionToken: session, expiresIn: 1800 } });
    }
    if (path === '/api/v1/status') {
      return route.fulfill({ json: runtimeStatus });
    }
    if (['/api/v1/presets', '/api/v1/effects', '/api/v1/schedules', '/api/v1/cue-lists', '/api/v1/config', '/api/v1/auth/ws-ticket'].includes(path)) {
      if (delaySecondary) {
        await new Promise(r => setTimeout(r, 600));
      }
      return route.fulfill({ json: path === '/api/v1/auth/ws-ticket' ? { ticket: 'b'.repeat(32), expiresInMs: 15000 } : [] });
    }
    return route.fulfill({ json: {} });
  });

  await page.goto('http://192.168.4.1');
  await expect(page.locator('#normal-login')).toBeVisible();
  await page.locator('#experience-token').fill(key);
  await page.getByRole('button', { name: 'Connect to Node' }).click();
  // Shell becomes visible immediately after /status even while secondary calls are still pending
  await expect(page.locator('#product-app')).toBeVisible({ timeout: 400 });
  delaySecondary = false;
});

test('embedded System Access creates/disables accounts without displaying saved keys', async ({ page }) => {
  const calls = await nodeFixture(page, true);
  await page.goto('http://192.168.4.1');
  await page.locator('#experience-token').fill(key);
  await page.getByRole('button', { name: 'Connect to Node' }).click();
  await expect(page.locator('#product-app')).toBeVisible();
  await page.locator('[data-page="diagnostics"]').click();
  await expect(page.locator('#access-roles')).toContainText('Installer — Not configured');
  await page.locator('#access-role').selectOption('installer');
  await page.locator('#system-access').getByRole('button', { name: 'Generate Secure Key' }).click();
  const generated = await page.locator('#access-key').inputValue();
  expect(generated).toHaveLength(48);
  await page.getByRole('button', { name: 'Save Access Key', exact: true }).click();
  await expect(page.locator('#access-roles')).toContainText('Installer — Configured');
  await expect(page.locator('#access-key')).toHaveValue('');
  await expect(page.locator('#system-access')).not.toContainText(generated);
  expect(calls.some(c => c.body?.role === 'installer' && c.body?.token === generated)).toBe(true);
  await page.getByRole('button', { name: 'Disable Installer', exact: true }).click();
  await expect(page.locator('#access-roles')).toContainText('Installer — Not configured');
});

test('embedded desktop and mobile retain last loaded scenes during connection loss', async ({ page }, testInfo) => {
  await page.clock.install();
  await nodeFixture(page, true);
  let offline = false;
  const streams: any[] = [];
  const storedScene = { index: 0, name: 'Evening wash', globalBrightness: 160, transitionMs: 1200,
    channels: Array.from({ length: 16 }, (_, index) => ({ layers: [{ effect: 'solid', enabled: index < 8,
      primaryColor: { r: 255, g: 45, b: 85 }, secondaryColor: { r: 0, g: 0, b: 0 }, opacity: 255 }] })) };
  await page.route('http://192.168.4.1/api/v1/status', route => offline ? route.abort() : route.fulfill({ json: runtimeStatus }));
  await page.route('http://192.168.4.1/api/v1/presets', route => offline ? route.abort() : route.fulfill({ json: [storedScene] }));
  await page.routeWebSocket('ws://192.168.4.1/ws', socket => {
    streams.push(socket);
    if (offline) socket.close();
    else socket.send(JSON.stringify(runtimeStatus));
  });
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('http://192.168.4.1');
  await page.locator('#experience-token').fill(key);
  await page.getByRole('button', { name: 'Connect to Node' }).click();
  await expect(page.locator('#connection-state')).toHaveText('Live status');
  await expect(page.locator('#dash-playing')).toHaveText('Evening wash');
  await expect(page.locator('#dash-power-max')).toHaveText('Budget: 10000 mW');
  await page.screenshot({ path: testInfo.outputPath('embedded-desktop.png'), fullPage: true, animations: 'disabled' });
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(page.locator('#dash-quick-scenes')).toContainText('Evening wash');
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
  await page.screenshot({ path: testInfo.outputPath('embedded-mobile.png'), fullPage: true, animations: 'disabled' });
  offline = true;
  for (const socket of streams) socket.close();
  await page.clock.fastForward(11000);
  await page.locator('#refresh-button').click();
  await expect(page.locator('#connection-state')).toHaveText('Offline');
  await expect(page.locator('#connection-detail')).toContainText('Node status is stale');
  await expect(page.locator('#dash-quick-scenes')).toContainText('Evening wash');
  await page.screenshot({ path: testInfo.outputPath('embedded-mobile-offline.png'), fullPage: true, animations: 'disabled' });
  offline = false;
  await page.locator('#refresh-button').click();
  await expect(page.locator('#connection-detail')).not.toContainText('Node status is stale');
  await page.getByRole('button', { name: 'Disconnect', exact: true }).click();
  await expect(page.locator('#experience-auth')).toBeVisible();
  await expect(page.locator('#product-app')).toBeHidden();
});
