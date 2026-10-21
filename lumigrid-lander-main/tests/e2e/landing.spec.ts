import { test, expect } from '@playwright/test';

test('public capabilities link to working source guides', async ({ page }, testInfo) => {
  await page.goto('/');
  await expect(page.getByTestId('hero-title')).toBeVisible();
  await page.screenshot({ path: testInfo.outputPath('landing-desktop.png') });
  await page.getByRole('link', { name: 'Set up your node', exact: true }).first().click();
  await expect(page.getByRole('heading', { name: 'Getting started', exact: true })).toBeVisible();
  await page.getByRole('link', { name: 'flash layout', exact: true }).click();
  await expect(page.getByRole('heading', { name: 'Flash layout and updates', exact: true })).toBeVisible();
});

test('documentation renders on direct load and unknown guides return 404', async ({ page }) => {
  await page.goto('/docs/API_REFERENCE');
  await expect(page.getByRole('heading', { name: 'HTTP API reference' })).toBeVisible();
  await expect(page.getByRole('cell', { name: '/api/v1/ota', exact: true })).toBeVisible();
  const response = await page.goto('/docs/not-a-real-guide');
  expect(response?.status()).toBe(404);
});

test('language selection changes public content and mobile layout fits', async ({ page }, testInfo) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto('/');
  await page.getByRole('combobox').selectOption('lv');
  await expect(page.getByTestId('hero-title')).toHaveText('Katrai gaismai sava loma.');
  await page.getByRole('combobox').selectOption('ru');
  await expect(page.getByTestId('hero-title')).toHaveText('У каждого света своя роль.');
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.screenshot({ path: testInfo.outputPath('landing-mobile.png'), fullPage: true });
});

test('history is labeled and flasher does not offer a nonexistent release', async ({ page }) => {
  await page.goto('/docs/history/2026-10-21/LG-P0-BOOT-001_REPORT');
  await expect(page.getByText('Historical record.', { exact: false }).first()).toBeVisible();
  await page.goto('/flasher');
  await expect(page.getByRole('heading', { name: 'Build and install firmware' })).toBeVisible();
  await expect(page.locator('esp-web-install-button')).toHaveCount(0);
});
