import { test, expect, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const key = 'MyChosenAdminAccessKey2026!';
const session = 'a'.repeat(64);
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
      return route.fulfill({ contentType: 'text/html', body: readFileSync(resolve('embedded/dashboard.html'), 'utf8') });
    }
    let response: any = {};
    if (path === '/api/v1/auth/status') response = { provisioned, bootstrapAllowed: !provisioned && allowed, factorySetupAllowed: !provisioned && allowed };
    if (path === '/api/v1/auth/session') response = { authenticated: true, role: 'admin', sessionToken: session, expiresIn: 1800, setupRequired: !provisioned };
    if (path === '/api/v1/auth/bootstrap') {
      expect(body.adminToken).toBe(key);
      provisioned = true;
      response = { provisioned: true, authenticated: true, role: 'admin', sessionToken: session, expiresIn: 1800 };
    }
    if (['/api/v1/presets', '/api/v1/effects', '/api/v1/schedules', '/api/v1/cue-lists'].includes(path)) response = [];
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
  await expect(page.locator('#setup-defaults')).toContainText('lumigrid123');
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
  expect(await page.evaluate(() => sessionStorage.getItem('lg_node_token'))).toBe(session);
  expect(calls.filter(c => c.path.endsWith('/session')).map(c => c.body?.token)).toEqual(['lumigrid123']);
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
  await expect(page.locator('.pin-card')).toContainText('lumigrid123');
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
  expect(requests[0].headers['x-api-key']).toBe('lumigrid123');
  expect(calls.filter(c => c.path.endsWith('/session') && c.body?.token === key)).toHaveLength(0);
});

test('Svelte provisioned node shows normal access-key login (O)', async ({ page }) => {
  await nodeFixture(page, true);
  await openExternal(page);
  await expect(page.getByPlaceholder('Node Access Key', { exact: true })).toBeVisible();
  await expect(page.getByText('Secure your LumiGrid')).toHaveCount(0);
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
