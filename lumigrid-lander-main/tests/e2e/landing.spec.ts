import { test, expect } from '@playwright/test';

test('LED Node presentation links to useful setup guides', async ({ page }, testInfo) => {
  await page.goto('/');
  await expect(page.getByTestId('hero-title')).toBeVisible();
  const visible = await page.locator('.public-landing').innerText();
  expect(visible).toContain('LED NODE');
  expect(visible).toContain('Animated LED strips');
  expect(visible).toContain('Dimmable lighting');
  expect(visible).not.toMatch(/\b(?:ESP32|PCA9685|WS2812|SK6812|PWM|RMT)\b/i);
  await page.screenshot({ path: testInfo.outputPath('landing-desktop.png') });
  await page.getByRole('link', { name: 'Get started', exact: true }).first().click();
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
  await expect(page.getByTestId('hero-title')).toHaveText('Visas jūsu gaismas. Vienuviet.');
  await page.getByRole('combobox').selectOption('ru');
  await expect(page.getByTestId('hero-title')).toHaveText('Весь ваш свет. В одном месте.');
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.screenshot({ path: testInfo.outputPath('landing-mobile.png'), fullPage: true });
});

test('history is labeled and advanced updates do not invent a release', async ({ page }) => {
  await page.goto('/docs/history/2026-10-21/LG-P0-BOOT-001_REPORT');
  await expect(page.getByText('Historical record.', { exact: false }).first()).toBeVisible();
  await page.goto('/flasher');
  await expect(page.getByRole('heading', { name: 'LED Node setup and updates' })).toBeVisible();
  await expect(page.locator('esp-web-install-button')).toHaveCount(0);
});
