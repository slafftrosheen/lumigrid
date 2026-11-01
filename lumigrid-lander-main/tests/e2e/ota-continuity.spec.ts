import { test, expect, type Page, type Route } from '@playwright/test';

const a = '192.168.1.50', b = '192.168.1.60';
const nodeId = (ip: string) => `node-${ip}`;
const headers = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'Authorization, Content-Type, X-API-Key, X-LumiGrid-Node-Id',
  'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
};
const image = { name: 'lumigrid-application.bin', mimeType: 'application/octet-stream', buffer: Buffer.from([0xe9, 1, 2, 3]) };
const identity = (ip: string) => ({ id: nodeId(ip), name: `Node ${ip}`, family: 'led-node',
  model: 'LumiGrid LED Node', hardwareRevision: 'A', firmwareVersion: '1.2.0' });
const ota = (ip: string) => ({ nodeId: nodeId(ip), targetBinding: true, state: 'confirmed',
  runningVersion: '1.2.0', targetVersion: '1.2.0', rolledBack: false,
  runningPartition: 'ota_0', targetPartition: 'ota_0', nextPartition: 'ota_1',
  maxFirmwareBytes: 4 * 1048576, imageBytes: 1048576 });
const receipt = (ip: string) => ({ accepted: true, state: 'pending_reboot', commandId: 9,
  jobId: 4, targetVersion: '1.2.0', projectName: 'lumigrid_led_node', nodeId: nodeId(ip), targetPartition: 'ota_1' });
const confirmed = (ip: string) => ({ ...ota(ip), runningPartition: 'ota_1', targetPartition: 'ota_1', nextPartition: 'ota_0' });
const runtime = (ip: string) => ({
  schema: 3, streamSeq: 1, generatedAtMs: 12000, node: { id: nodeId(ip), bootSessionId: 1 },
  runtime: { effect: 'solid', brightness: 160, testActive: false, autoLoop: false, controlOwner: 'ScenePlayback', frameCount: 400 },
  output: { enabled: true, powerMw: 1200, state: 'healthy', fault: false, faultCount: 0, estopActive: false },
  scene: { activePreset: 0, requestedPreset: 0, requestedGeneration: 1, appliedGeneration: 1, applyPending: false, transitioning: false, fallbackActive: false },
  manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
  transport: { mode: 'scene', generation: 1, sceneIndex: 0, timeline: { running: false, bpm: 120 }, cue: { running: false } },
  sync: { configuredRole: 'auto', activeRole: 'leader', clockLocked: false },
  network: { state: 'connected', rssi: -52, staConnected: true, staIp: ip, apActive: false, applyState: 'idle' },
  protocols: { artnet: { enabled: true, sources: 0, packetsPerSecond: 0 }, sacn: { enabled: false, sources: 0, packetsPerSecond: 0 } },
  timeline: { clockMs: 12000, driftPpm: 0, synced: false }, automation: { activeTracks: 0, transitionCurve: 'linear' }, job: null, channels: [],
});
const health = { schema_version: 1, system: { uptime_s: 100, free_heap: 50000, min_free_heap: 40000, fps: 60 },
  network: { wifi_rssi: -52 }, nvs: { used: 10, free: 90 }, config: { valid: true, error_count: 0 },
  startup: { state: 'ok', stage: 'app', failing_stage: '', failing_error: 0 },
  output: { state: 'healthy', fault: false, current_power_mw: 1200 }, dmx: { sources_active: 0 }, sync: { packet_loss: 0 } };

function latch() {
  let release!: () => void;
  const promise = new Promise<void>(resolve => { release = resolve; });
  return { promise, release };
}
type Override = (route: Route, ip: string, path: string) => Promise<boolean>;
async function fixture(page: Page, override?: Override) {
  const calls: { ip: string; path: string; method: string; headers: Record<string, string> }[] = [];
  await page.addInitScript(({ a, b }) => {
    localStorage.setItem('lg_node_ip', a);
    localStorage.setItem('lg_nodes', JSON.stringify([a, b]));
    localStorage.setItem('lg_nodes_v3', JSON.stringify(Object.fromEntries([a, b].map(ip => [`node-${ip}`, {
      id: `node-${ip}`, deviceName: `Node ${ip}`, address: ip, family: 'led-node', model: 'lg-led-v1',
      firmware: '1.2.0', presence: 'online', connection: 'online', lastSeen: Date.now(), discoverySource: 'manual',
    }]))));
    sessionStorage.setItem('lg_node_sessions', JSON.stringify([a, b].map(ip => ({
      address: ip, nodeId: `node-${ip}`, role: 'admin', sessionToken: `session-${ip}`, expiresAt: Date.now() + 3600000,
    }))));
  }, { a, b });
  for (const ip of [a, b]) {
    await page.route(`http://${ip}/**`, async route => {
      const request = route.request(), path = new URL(request.url()).pathname;
      calls.push({ ip, path, method: request.method(), headers: request.headers() });
      if (request.method() === 'OPTIONS') { await route.fulfill({ status: 204, headers }); return; }
      if (override && await override(route, ip, path)) return;
      let response: unknown = {};
      if (path === '/api/v1/auth/status') response = { provisioned: true, bootstrapAllowed: false };
      if (path === '/api/v1/auth/session') response = { authenticated: true, role: 'admin', sessionToken: `session-${ip}`, expiresIn: 1800 };
      if (path === '/api/v1/auth/ws-ticket') response = { ticket: 'b'.repeat(32), expiresInMs: 15000 };
      if (path === '/api/v1/status') response = runtime(ip);
      if (path === '/api/v1/health') response = health;
      if (path === '/api/v1/info' || path === '/api/v1/identity') response = identity(ip);
      if (path === '/api/v1/capabilities') response = {
        schema: 1,
        node: { family: 'led-node', model: 'lg-led-v1', firmware: '1.2.0', hardwareRevision: 'A' },
        features: { ota: true },
      };
      if (path === '/api/v1/config') response = { deviceName: `Node ${ip}`, limits: {}, physicalOutputs: [], fixtures: [] };
      if (path === '/api/v1/recovery/status') response = { safeMode: false, configValid: true, commissioningState: 'none',
        startupState: 'ok', failingStage: '', failingError: 0, recoveryNetworkActive: false, outputsForcedOff: false, otaState: 'confirmed', restoreState: 'none' };
      if (path === '/api/v1/ota/status') response = ota(ip);
      if (path === '/api/v1/ota' && request.method() === 'POST') response = receipt(ip);
      await route.fulfill({ json: response, headers });
    });
    await page.routeWebSocket(`ws://${ip}/ws`, socket => { socket.send(JSON.stringify(runtime(ip))); });
  }
  return calls;
}

async function openSingleNode(page: Page) {
  await page.goto('/dashboard/firmware');
  await expect(page.getByRole('heading', { name: 'System & Recovery', exact: true })).toBeVisible();
  await expect(page.locator('.node-badge')).toContainText(nodeId(a));
  await page.getByRole('button', { name: 'Firmware OTA', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Install Firmware', exact: true })).toBeVisible();
}
async function selectImage(page: Page) {
  await page.locator('#ota-file-input').setInputFiles(image);
  await expect(page.locator('#ota-upload-btn')).toBeEnabled();
}
async function expectUsableMobileTabs(page: Page) {
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
  expect(await page.locator('.system-tabs .tab-btn').evaluateAll(buttons =>
    buttons.every(button => button.scrollWidth <= button.clientWidth + 1))).toBe(true);
  expect(await page.locator('.system-tabs').evaluate(nav => nav.scrollWidth > nav.clientWidth)).toBe(true);
}

test('same-version update cannot confirm the old partition and renders at desktop and mobile widths', async ({ page }, testInfo) => {
  await page.clock.install();
  let uploaded = false, candidateReady = false, verificationReads = 0;
  const calls = await fixture(page, async (route, ip, path) => {
    if (path === '/api/v1/ota' && route.request().method() === 'POST') {
      uploaded = true; await route.fulfill({ json: receipt(ip), headers }); return true;
    }
    if (path === '/api/v1/ota/status' && uploaded) {
      verificationReads++; await route.fulfill({ json: candidateReady ? confirmed(ip) : ota(ip), headers }); return true;
    }
    return false;
  });
  await page.setViewportSize({ width: 1440, height: 1000 });
  await openSingleNode(page); await selectImage(page);
  await page.screenshot({ path: testInfo.outputPath('ota-desktop-ready.png'), fullPage: true, animations: 'disabled' });
  await page.locator('#ota-upload-btn').click();
  await expect(page.locator('.ota-panel')).toContainText('Validated candidate');
  expect(calls.find(call => call.path === '/api/v1/ota' && call.method === 'POST')?.headers['x-lumigrid-node-id']).toBe(nodeId(a));
  await page.clock.fastForward(3000);
  await expect(page.locator('.ota-panel')).toContainText('Waiting for node');
  await page.clock.fastForward(3000);
  await expect.poll(() => verificationReads).toBe(1);
  await expect(page.locator('.ota-panel')).toContainText('Waiting for node');
  await expect(page.getByText('Firmware update confirmed', { exact: true })).toHaveCount(0);
  await page.setViewportSize({ width: 390, height: 844 });
  await expectUsableMobileTabs(page);
  await page.screenshot({ path: testInfo.outputPath('ota-mobile-waiting.png'), fullPage: true, animations: 'disabled' });
  candidateReady = true;
  await page.clock.fastForward(3000);
  await expect(page.getByText('Firmware update confirmed', { exact: true })).toBeVisible();
});

test('switching nodes cancels a pending upload result, including A to B to A', async ({ page }) => {
  await page.clock.install();
  const requested = latch(), release = latch(), handled = latch();
  const calls = await fixture(page, async (route, ip, path) => {
    if (ip !== a || path !== '/api/v1/ota' || route.request().method() !== 'POST') return false;
    requested.release(); await release.promise;
    await route.fulfill({ json: receipt(ip), headers }).catch(() => {});
    handled.release(); return true;
  });
  await openSingleNode(page); await selectImage(page); await page.locator('#ota-upload-btn').click();
  await requested.promise;
  await page.locator('.node-select').selectOption(b);
  await expect(page.locator('.node-badge')).toContainText(nodeId(b));
  await expect(page.locator('.ota-panel')).toContainText('Select a LumiGrid application image');
  const reads = calls.filter(call => call.ip === a && call.path === '/api/v1/ota/status').length;
  release.release(); await handled.promise;
  await page.clock.fastForward(10000);
  expect(calls.filter(call => call.ip === a && call.path === '/api/v1/ota/status')).toHaveLength(reads);
  expect(calls.filter(call => call.ip === b && call.path === '/api/v1/ota' && call.method === 'POST')).toHaveLength(0);
  await expect(page.getByText('Firmware update confirmed', { exact: true })).toHaveCount(0);
  await page.locator('.node-select').selectOption(a);
  await expect(page.locator('.node-badge')).toContainText(nodeId(a));
  await expect(page.locator('.ota-panel .file-name')).toHaveCount(0);
  await expect(page.locator('#ota-select-label')).toBeVisible();
});

test('switching nodes cancels an in-flight confirmation poll and discards its successful result', async ({ page }) => {
  await page.clock.install();
  let uploaded = false;
  const requested = latch(), release = latch(), handled = latch();
  const calls = await fixture(page, async (route, ip, path) => {
    if (path === '/api/v1/ota' && route.request().method() === 'POST') {
      uploaded = true; await route.fulfill({ json: receipt(ip), headers }); return true;
    }
    if (ip === a && path === '/api/v1/ota/status' && uploaded) {
      requested.release(); await release.promise;
      await route.fulfill({ json: confirmed(ip), headers }).catch(() => {});
      handled.release(); return true;
    }
    return false;
  });
  await openSingleNode(page); await selectImage(page); await page.locator('#ota-upload-btn').click();
  await expect(page.locator('.ota-panel')).toContainText('Validated candidate');
  await page.clock.fastForward(3000); await expect(page.locator('.ota-panel')).toContainText('Waiting for node');
  await page.clock.fastForward(3000); await requested.promise;
  await page.locator('.node-select').selectOption(b);
  await expect(page.locator('.node-badge')).toContainText(nodeId(b));
  const reads = calls.filter(call => call.ip === a && call.path === '/api/v1/ota/status').length;
  release.release(); await handled.promise;
  await page.clock.fastForward(10000);
  expect(calls.filter(call => call.ip === a && call.path === '/api/v1/ota/status')).toHaveLength(reads);
  await expect(page.locator('#ota-select-label')).toBeVisible();
  await expect(page.getByText('Firmware update confirmed', { exact: true })).toHaveCount(0);
});

test('unavailable slot blocks upload and the native file picker remains keyboard accessible', async ({ page }) => {
  const calls = await fixture(page, async (route, _ip, path) => {
    if (path !== '/api/v1/ota/status') return false;
    await route.fulfill({ status: 503, json: { error: 'storage_unavailable' }, headers }); return true;
  });
  await openSingleNode(page);
  await expect(page.locator('.ota-panel [role="alert"]')).toContainText('Could not read');
  const chooser = page.waitForEvent('filechooser');
  await page.locator('#ota-select-label').focus();
  await page.keyboard.press('Enter');
  await (await chooser).setFiles(image);
  await expect(page.locator('.ota-panel .file-name')).toContainText(image.name);
  await expect(page.locator('#ota-upload-btn')).toBeDisabled();
  await expect(page.getByRole('button', { name: 'Reload update status' })).toBeEnabled();
  expect(calls.filter(call => call.path === '/api/v1/ota' && call.method === 'POST')).toHaveLength(0);
});

test('a recoverable pre-upload read failure preserves the selected file for retry', async ({ page }) => {
  let reads = 0;
  const calls = await fixture(page, async (route, ip, path) => {
    if (path !== '/api/v1/ota/status') return false;
    reads++;
    if (reads === 2) await route.fulfill({ status: 503, json: { error: 'temporarily_unavailable' }, headers });
    else await route.fulfill({ json: ota(ip), headers });
    return true;
  });
  await openSingleNode(page); await selectImage(page); await page.locator('#ota-upload-btn').click();
  await expect(page.locator('.ota-panel [role="alert"]')).toContainText('temporarily_unavailable');
  await expect(page.locator('.ota-panel .file-name')).toContainText(image.name);
  await expect(page.locator('#ota-upload-btn')).toBeDisabled();
  await page.getByRole('button', { name: 'Reload update status', exact: true }).click();
  await expect(page.locator('#ota-upload-btn')).toBeEnabled();
  expect(calls.filter(call => call.path === '/api/v1/ota' && call.method === 'POST')).toHaveLength(0);
});

test('leaving fleet rollout cancels its canary and prevents later uploads', async ({ page }, testInfo) => {
  await page.clock.install();
  const requested = latch(), release = latch(), handled = latch();
  const calls = await fixture(page, async (route, ip, path) => {
    if (path !== '/api/v1/ota' || route.request().method() !== 'POST') return false;
    requested.release(); await release.promise;
    await route.fulfill({ json: receipt(ip), headers }).catch(() => {});
    handled.release(); return true;
  });
  await page.goto('/dashboard/firmware');
  await page.getByRole('button', { name: 'Fleet Rollout', exact: true }).click();
  await expect(page.locator('.fleet-ota-panel .node-chip')).toHaveCount(2);
  await page.locator('#fleet-file-input').setInputFiles(image);
  await page.getByRole('button', { name: 'Select All', exact: true }).click();
  await page.locator('#fleet-deploy-btn').click(); await requested.promise;
  await page.getByRole('button', { name: 'Status & Overview', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Hardware & Identity' })).toBeVisible();
  release.release(); await handled.promise; await page.clock.fastForward(100000);
  expect(calls.filter(call => call.path === '/api/v1/ota' && call.method === 'POST')).toHaveLength(1);
  await page.getByRole('button', { name: 'Fleet Rollout', exact: true }).click();
  await expect(page.locator('.fleet-ota-panel .progress-box')).toHaveCount(0);
  await page.setViewportSize({ width: 390, height: 844 });
  await expectUsableMobileTabs(page);
  await page.screenshot({ path: testInfo.outputPath('ota-fleet-mobile.png'), fullPage: true, animations: 'disabled' });
});

test('Fleet primary action keeps readable opaque contrast in both color schemes', async ({ page }, testInfo) => {
  await fixture(page);
  await page.goto('/dashboard/firmware');
  await page.getByRole('button', { name: 'Fleet Rollout', exact: true }).click();
  await page.locator('#fleet-file-input').setInputFiles(image);
  await page.getByRole('button', { name: 'Select All', exact: true }).click();
  const button = page.locator('#fleet-deploy-btn');
  await expect(button).toBeEnabled();
  for (const colorScheme of ['light', 'dark'] as const) {
    await page.emulateMedia({ colorScheme });
    const colors = await button.evaluate(element => {
      const style = getComputedStyle(element);
      const luminance = (value: string) => {
        const channels = value.match(/[\d.]+/g)!.slice(0, 3).map(Number).map(n => n / 255)
          .map(n => n <= 0.04045 ? n / 12.92 : ((n + 0.055) / 1.055) ** 2.4);
        return channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722;
      };
      const pair = [luminance(style.color), luminance(style.backgroundColor)].sort((a, b) => a - b);
      return { foreground: style.color, background: style.backgroundColor, opacity: style.opacity,
        ratio: (pair[1] + 0.05) / (pair[0] + 0.05) };
    });
    expect(colors.foreground).toBe('rgb(0, 0, 0)');
    expect(colors.background).toBe('rgb(255, 45, 85)');
    expect(colors.opacity).toBe('1');
    expect(colors.ratio).toBeGreaterThanOrEqual(4.5);
    await testInfo.attach(`fleet-button-${colorScheme}-contrast`, {
      body: JSON.stringify(colors, null, 2), contentType: 'application/json',
    });
  }
  await page.setViewportSize({ width: 390, height: 844 });
  await button.scrollIntoViewIfNeeded();
  await page.screenshot({ path: testInfo.outputPath('ota-fleet-primary-contrast.png'), animations: 'disabled' });
});
