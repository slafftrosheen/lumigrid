import { test, expect, type Page, type Route } from '@playwright/test';

const a = '192.168.1.50';
const b = '192.168.1.60';
const headers = {
  'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': 'Authorization, Content-Type, X-API-Key',
  'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
};
function deferred() {
  let resolve!: () => void;
  const promise = new Promise<void>(done => { resolve = done; });
  return { promise, resolve };
}
const definition = (id: string, requiredEvidence: string) => ({ id, category: id.startsWith('BOOT') ? 'BOOT' : 'POWER',
  title: id.startsWith('BOOT') ? 'Boot Sequence & Identity Assertion' : 'Power Limiter Load Measurement',
  requiredEvidence, reqDev: 'optional', reqProd: 'required', reqRc: 'required', timeoutMs: 300000,
  expectedValue: 0, tolerance: 5000, unit: requiredEvidence === 'instrument_measurement' ? 'mW' : '',
});
const plan = { schema: 1, planVersion: 2, tests: [definition('BOOT-001', 'automatic'), definition('POWER-LIMIT-001', 'instrument_measurement')] };
const result = (id: string, state = 'pending') => ({ id, state,
  evidenceSource: id.startsWith('BOOT') ? 'automatic' : 'instrument_measurement', startedMs: state === 'pending' ? 0 : 12000,
  completedMs: ['passed', 'failed'].includes(state) ? 18000 : 0, attempts: state === 'pending' ? 0 : 1,
  measuredValue: 0, unit: id.startsWith('BOOT') ? '' : 'mW', note: '', errorCode: 0,
});
const acceptance = (ip: string, waiting = false) => ({ schema: 1, certificationId: `cert-${ip}`, nodeId: `node-${ip}`,
  profile: 'production_unit', planVersion: 2, state: waiting ? 'in_progress' : 'incomplete', firmwareVersion: 'tested-v1',
  hardwareRevision: '', serialNumber: `SN-${ip}`, passed: 0, failed: 0, skipped: 0, total: 2, pending: 2, stale: false,
  evidenceBinding: 'session_attempt_v1', cancellationPending: false, confirmationPending: false,
  currentTest: waiting ? 'POWER-LIMIT-001' : '', tests: [result('BOOT-001'), result('POWER-LIMIT-001', waiting ? 'waiting_for_operator' : 'failed')],
});
const runtime = (ip: string) => ({ schema: 3, streamSeq: 1, generatedAtMs: 12000, node: { id: `node-${ip}`, bootSessionId: 1 },
  runtime: { effect: 'solid', brightness: 160, testActive: false, autoLoop: false, controlOwner: 'ScenePlayback', frameCount: 400 },
  output: { enabled: false, powerMw: 0, state: 'healthy', fault: false, faultCount: 0, estopActive: false,
    power: { estimatedRequestedMw: 0, estimatedLimitedMw: 0, estimateComplete: true, limitMw: 10000 } },
  scene: { activePreset: 0, requestedPreset: 0, requestedGeneration: 1, appliedGeneration: 1, applyPending: false, transitioning: false, fallbackActive: false },
  manual: { active: false, channelMask: 0, leaseRemainingMs: 0 },
  transport: { mode: 'scene', generation: 1, sceneIndex: 0, timeline: { running: false, bpm: 120 }, cue: { running: false } },
  sync: { configuredRole: 'auto', activeRole: 'leader', clockLocked: false },
  network: { state: 'connected', rssi: -52, staConnected: true, staIp: ip, apActive: false, applyState: 'idle' },
  protocols: { artnet: { enabled: true, sources: 0, packetsPerSecond: 0 }, sacn: { enabled: false, sources: 0, packetsPerSecond: 0 } },
  timeline: { clockMs: 12000, driftPpm: 0, synced: false }, automation: { activeTracks: 0, transitionCurve: 'linear' }, job: null, channels: [],
});
type Override = (route: Route, ip: string, path: string) => Promise<boolean>;
async function fixture(page: Page, waiting = false, override?: Override) {
  const states: Record<string, ReturnType<typeof acceptance>> = { [a]: acceptance(a, waiting), [b]: acceptance(b, waiting) };
  const calls: { ip: string; path: string; method: string; body: any }[] = [];
  await page.addInitScript(({ a, b }) => {
    localStorage.setItem('lg_node_ip', a); localStorage.setItem('lg_nodes', JSON.stringify([a, b]));
    sessionStorage.setItem('lg_node_sessions', JSON.stringify([a, b].map(ip => ({
      address: ip, nodeId: `node-${ip}`, role: 'admin', sessionToken: `session-${ip}`, expiresAt: Date.now() + 3600000,
    }))));
  }, { a, b });
  for (const ip of [a, b]) {
    await page.route(`http://${ip}/**`, async route => {
      const request = route.request(); const path = new URL(request.url()).pathname;
      if (request.method() === 'OPTIONS') { await route.fulfill({ status: 204, headers }); return; }
      calls.push({ ip, path, method: request.method(), body: request.postData() ? request.postDataJSON() : null });
      if (override && await override(route, ip, path)) return;
      let response: any = {};
      if (path === '/api/v1/auth/status') response = { provisioned: true, bootstrapAllowed: false };
      if (path === '/api/v1/auth/session') response = { authenticated: true, role: 'admin', sessionToken: `session-${ip}`, expiresIn: 1800 };
      if (path === '/api/v1/auth/ws-ticket') response = { ticket: 'b'.repeat(32), expiresInMs: 15000 };
      if (path === '/api/v1/status') response = runtime(ip);
      if (path === '/api/v1/info') response = { id: `node-${ip}`, name: `Node ${ip}`, firmwareVersion: 'tested-v1' };
      if (path === '/api/v1/health') response = { schema_version: 1, system: { uptime_s: 100, free_heap: 50000, min_free_heap: 40000, reset_reason: 1, fps: 60 },
        network: { wifi_rssi: -52 }, nvs: { used: 10, free: 90 }, config: { valid: true, error_count: 0, persistence_state: 'saved' },
        startup: { state: 'ok' }, output: { state: 'healthy', fault: false, current_power_mw: 0 }, dmx: {}, sync: {} };
      if (path === '/api/v1/certification/plan') response = plan;
      if (path === '/api/v1/certification/status') response = states[ip];
      if (path === '/api/v1/factory/status') response = { state: 'idle', effect: 'blackout', effectStartedMs: 0, durationMs: 0,
        brightness: 0, autoLoop: false, outputsEnabled: false, testActive: false, commandRejections: 0, session: null };
      if (path === '/api/v1/certification/test/start') {
        const body = request.postDataJSON();
        const index = states[ip].tests.findIndex(test => test.id === body.testId);
        states[ip].tests[index] = { ...result(body.testId, 'passed'), note: 'Firmware probe completed' };
        states[ip].currentTest = ''; response = { status: 'started' };
      }
      if (path === '/api/v1/certification/test/result') {
        const body = request.postDataJSON();
        states[ip].tests[1] = { ...result('POWER-LIMIT-001', body.passed ? 'passed' : 'failed'), measuredValue: body.measuredValue, unit: body.unit, note: body.note };
        states[ip].currentTest = ''; states[ip].failed = body.passed ? 0 : 1;
        response = { status: 'recorded', overallState: states[ip].state };
      }
      if (path === '/api/v1/certification/report') response = {
        schema: 'lumigrid-certification-v1', ...states[ip], state: 'incomplete', stale: true, staleReason: 'Configuration changed',
        reportingFirmwareVersion: 'exporter-v2', reportingFirmwareElfSha256: 'f'.repeat(64),
        startedMs: 12000, completedMs: 18000, testCount: 2, passedCount: 0, failedCount: 1, skippedCount: 0,
        configFingerprint: 123, projectName: 'LumiGrid',
        tests: states[ip].tests.map(test => ({ ...test, category: test.id.startsWith('BOOT') ? 'BOOT' : 'POWER', requirement: 'required',
          note: test.id === 'POWER-LIMIT-001' ? test.note || 'Method: bench meter; Notes: zero load' : test.note })), burnIn: { durationS: 0 },
      };
      await route.fulfill({ json: response, headers });
    });
    await page.routeWebSocket(`ws://${ip}/ws`, socket => socket.send(JSON.stringify(runtime(ip))));
  }
  return { states, calls };
}

test('delayed session and report responses cannot cross A→B→A or authentication replacement', async ({ page }) => {
  const sessionResponse = deferred(); const reportResponse = deferred(); let reportStarted = false;
  const { calls } = await fixture(page, false, async (route, ip, path) => {
    if (ip === a && path === '/api/v1/certification/start') {
      await sessionResponse.promise;
      await route.fulfill({ json: { certificationId: 'obsolete-start-a', profile: 'production_unit', state: 'in_progress' }, headers }); return true;
    }
    if (ip === a && path === '/api/v1/certification/report') {
      reportStarted = true; await reportResponse.promise;
      await route.fulfill({ json: { certificationId: `cert-${a}`, nodeId: `node-${a}`, tests: [] }, headers }); return true;
    }
    return false;
  });
  await page.goto('/dashboard/certification');
  await expect(page.getByRole('heading', { name: 'Hardware Acceptance Bench', exact: true })).toBeVisible();
  await expect(page.locator('.tiles-row').nth(0).locator('.cert-tile-port')).toHaveText(['A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8']);
  await expect(page.locator('.tiles-row').nth(1).locator('.cert-tile-port')).toHaveText(['P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8']);
  await expect(page.getByRole('button', { name: 'Start New Session', exact: true })).toBeEnabled();
  await page.getByLabel('Serial Number (required)', { exact: true }).fill('ONLY-A');
  await page.getByRole('button', { name: 'Start New Session', exact: true }).click();
  await page.locator('.node-select').selectOption(b);
  await expect(page.locator('.meta-grid')).toContainText(`cert-${b}`);
  await page.locator('.node-select').selectOption(a);
  await expect(page.locator('.meta-grid')).toContainText(`cert-${a}`);
  const arrived = page.waitForResponse(`http://${a}/api/v1/certification/start`);
  sessionResponse.resolve(); await arrived;
  await expect(page.locator('.cert-container')).not.toContainText('obsolete-start-a');
  const starts = calls.filter(call => call.path === '/api/v1/certification/start');
  expect(starts).toHaveLength(1);
  expect(starts[0]).toMatchObject({ ip: a, body: { profile: 'production_unit', serialNumber: 'ONLY-A', certificationId: `cert-${a}` } });
  await page.getByRole('button', { name: 'Acceptance Report', exact: true }).click();
  await expect.poll(() => reportStarted).toBe(true);
  await page.evaluate(async ip => {
    // Exercise the real store's same-address credential replacement, as on reauthentication.
    const modulePath = '/src/lib/auth/nodeAuth.ts';
    const { nodeAuthStore } = await import(modulePath);
    const session = nodeAuthStore.getSession(ip)!;
    nodeAuthStore.setSession({ ...session, sessionToken: 'rotated-session', expiresAt: Date.now() + 3600000 });
  }, a);
  const reportArrived = page.waitForResponse(`http://${a}/api/v1/certification/report`);
  reportResponse.resolve(); await reportArrived;
  await expect(page.getByRole('dialog')).toHaveCount(0);
  await expect(page.locator('.meta-grid')).toContainText(`cert-${a}`);
});

test('zero failed instrument evidence keeps method, survives a denied command, and owns busy dismissal', async ({ page }) => {
  const evidenceResponse = deferred(); let evidenceAttempts = 0;
  const { calls } = await fixture(page, true, async (route, _ip, path) => {
    if (path !== '/api/v1/certification/test/result') return false;
    evidenceAttempts++;
    if (evidenceAttempts === 1) {
      await route.fulfill({ status: 403, json: { error: 'Evidence temporarily denied' }, headers }); return true;
    }
    await evidenceResponse.promise;
    return false;
  });
  await page.goto('/dashboard/certification');
  await page.getByRole('row').filter({ hasText: 'POWER-LIMIT-001' }).getByRole('button', { name: 'Inspect Test' }).click();
  const dialog = page.getByRole('dialog');
  await page.getByLabel('Measured Value', { exact: true }).fill('0');
  await page.getByLabel('Measurement Instrument / Method', { exact: true }).fill('Bench meter');
  await page.getByLabel('Notes', { exact: true }).fill('No load');
  await page.getByRole('button', { name: 'Record Failure', exact: true }).click();
  await expect(page.getByRole('button', { name: 'Confirm Test Failure' })).toBeDisabled();
  await page.getByLabel('Failure Notes', { exact: true }).fill('No response');
  await page.getByRole('button', { name: 'Confirm Test Failure' }).click();
  await expect(dialog.getByRole('alert')).toContainText('Evidence temporarily denied');
  // A real successful status poll restores readiness but cannot erase a failed command.
  await expect(page.getByRole('button', { name: 'Confirm Test Failure' })).toBeEnabled({ timeout: 6000 });
  await expect(dialog.getByRole('alert')).toContainText('Evidence temporarily denied');
  await expect(page.getByLabel('Measured Value', { exact: true })).toHaveValue('0');
  await page.getByRole('button', { name: 'Confirm Test Failure' }).click();
  await expect(page.getByRole('button', { name: 'Close', exact: true })).toBeDisabled();
  await page.keyboard.press('Escape'); await expect(dialog).toBeVisible();
  evidenceResponse.resolve();
  await expect(dialog).toContainText('Node recorded POWER-LIMIT-001 as failed.');
  const posts = calls.filter(call => call.path === '/api/v1/certification/test/result');
  expect(posts).toHaveLength(2);
  expect(posts[1].body).toEqual({ testId: 'POWER-LIMIT-001', certificationId: `cert-${a}`, testStartedMs: 12000, testAttempt: 1,
    passed: false, evidenceSource: 'instrument_measurement', measuredValue: 0, unit: 'mW',
    note: 'Method: Bench meter; Notes: No load; [no_output] No response' });
  await page.getByRole('button', { name: 'Close', exact: true }).click();
  await expect(page.getByRole('row').filter({ hasText: 'POWER-LIMIT-001' })).toContainText('0 mW');
  await page.getByRole('row').filter({ hasText: 'BOOT-001' }).getByRole('button', { name: 'Inspect Test' }).click();
  await expect(dialog).toContainText('The firmware owns its result');
  await expect(dialog.getByRole('button', { name: 'Record Pass', exact: true })).toHaveCount(0);
  await expect(dialog.getByRole('button', { name: 'Record Failure', exact: true })).toHaveCount(0);
  await page.getByRole('button', { name: 'Run Firmware Probe' }).click();
  await expect(dialog).toContainText('Firmware probe completed');
  expect(calls.filter(call => call.path === '/api/v1/certification/test/start')[0].body).toEqual({ testId: 'BOOT-001', certificationId: `cert-${a}` });
});

test('report uses uptime and zero evidence, contains its table on mobile, and restores keyboard focus; stale status blocks mutations', async ({ page }, testInfo) => {
  await page.clock.install();
  let holdStatus = false; const statusResponse = deferred();
  const { states } = await fixture(page, false, async (route, _ip, path) => {
    if (path !== '/api/v1/certification/status' || !holdStatus) return false;
    await statusResponse.promise; return false;
  });
  await page.goto('/dashboard/certification');
  await expect(page.getByRole('button', { name: 'Start New Session', exact: true })).toBeEnabled();
  await expect(page.locator('.meta-grid')).toContainText('Unknown');
  const opener = page.getByRole('button', { name: 'Acceptance Report', exact: true });
  await opener.click();
  const dialog = page.getByRole('dialog');
  await expect(dialog).toContainText('12.000 s since device boot');
  await expect(dialog).toContainText('18.000 s since device boot');
  await expect(dialog).toContainText('Elapsed session duration: 6.000 s');
  await expect(dialog).not.toContainText('1970');
  await expect(dialog).toContainText('0 mW');
  await expect(dialog).toContainText('Method: bench meter; Notes: zero load');
  await expect(dialog).toContainText('incomplete');
  await expect(dialog).toContainText('Stale — Configuration changed');
  await expect(dialog).toContainText('This identifies the image exporting this report.');
  const matrix = dialog.getByRole('region', { name: 'Acceptance report verification matrix' });
  await expect(page.getByRole('button', { name: 'Close', exact: true })).toBeFocused();
  await page.keyboard.press('Shift+Tab');
  await expect(matrix).toBeFocused();
  await page.keyboard.press('Tab');
  await expect(page.getByRole('button', { name: 'Close', exact: true })).toBeFocused();
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
  const box = await dialog.boundingBox(); expect(box!.x).toBeGreaterThanOrEqual(0); expect(box!.x + box!.width).toBeLessThanOrEqual(390);
  expect(await dialog.locator('.test-table-container').evaluate(element => element.scrollWidth > element.clientWidth)).toBe(true);
  expect(await dialog.locator('.test-table-container').evaluate(element => element.clientHeight)).toBeGreaterThan(100);
  await page.keyboard.press('Shift+Tab');
  await expect(matrix).toBeFocused();
  await page.keyboard.press('ArrowRight');
  await expect.poll(() => matrix.evaluate(element => element.scrollLeft)).toBeGreaterThan(0);
  expect(await matrix.evaluate(element => getComputedStyle(element).outlineStyle)).toBe('solid');
  await page.keyboard.press('Tab');
  await expect(page.getByRole('button', { name: 'Close', exact: true })).toBeFocused();
  await page.screenshot({ path: testInfo.outputPath('acceptance-report-mobile.png'), animations: 'disabled' });
  await dialog.locator('.test-table-container').scrollIntoViewIfNeeded();
  await page.screenshot({ path: testInfo.outputPath('acceptance-report-table-mobile.png'), animations: 'disabled' });
  const downloadEvent = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Download JSON' }).click();
  const download = await downloadEvent;
  expect(download.suggestedFilename()).toBe(`lumigrid-acceptance-node-${a}-cert-${a}.json`);
  await page.keyboard.press('Escape'); await expect(dialog).toHaveCount(0); await expect(opener).toBeFocused();
  states[a].tests[1].errorCode = -1;
  states[a].tests[1].note = 'Factory sequence failed';
  await page.getByRole('button', { name: 'Refresh Status', exact: true }).click();
  const failedRow = page.getByRole('row').filter({ hasText: 'POWER-LIMIT-001' });
  await expect(failedRow).toContainText('Stored value (measurement unconfirmed): 0 mW');
  await failedRow.getByRole('button', { name: 'Inspect Test' }).click();
  await expect(dialog).toContainText('Stored value (measurement unconfirmed): 0 mW');
  await expect(dialog).not.toContainText('Recorded measurement: 0 mW');
  await page.keyboard.press('Escape');
  await opener.click();
  await expect(dialog).toContainText('Stored value (measurement unconfirmed): 0 mW');
  await expect(dialog).toContainText('Factory sequence failed');
  await page.keyboard.press('Escape');
  holdStatus = true;
  await page.clock.fastForward(11000);
  await expect(page.locator('.status-line')).toContainText('Acceptance status stale or unavailable');
  await expect(page.getByRole('button', { name: 'Start New Session', exact: true })).toBeDisabled();
  statusResponse.resolve();
});
