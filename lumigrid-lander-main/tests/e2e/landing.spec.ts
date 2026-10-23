import { test, expect } from '@playwright/test';

test('LED Node landing presents a product and routes customers to setup', async ({ page }, testInfo) => {
  await page.goto('/');
  await expect(page.getByTestId('hero-title')).toHaveText('Your lighting. One place to control it.');
  const copy = await page.locator('.public-landing').innerText();
  expect(copy).toContain('Animated LED strips');
  expect(copy).toContain('Dimmable lighting');
  expect(copy).toContain('Signs & displays');
  expect(copy).toContain('Interiors & accents');
  expect(copy).not.toMatch(/\b(?:ESP32|PCA9685|WS2812|SK6812|PWM|RMT|I2C|MCU)\b/i);
  await page.screenshot({ path: testInfo.outputPath('landing-product-desktop.png'), fullPage: true });

  await page.getByRole('link', { name: 'Get started', exact: true }).first().click();
  await expect(page).toHaveURL(/\/start$/);
  await expect(page.getByRole('heading', { name: 'Start with your LED Node' })).toBeVisible();
  await expect(page.getByRole('link', { name: /192\.168\.4\.1/ })).toHaveAttribute('href', 'http://192.168.4.1/');
  await expect(page.getByRole('link', { name: /Installer setup and maintenance/ })).toHaveAttribute('href', '/docs/GETTING_STARTED');
});

test('lighting preview changes visually without implying it controls the device', async ({ page }) => {
  await page.goto('/');
  const preview = page.getByTestId('preview-stage');
  await expect(preview).toHaveAttribute('data-preview', 'color');
  await page.getByRole('button', { name: 'Warm glow' }).click();
  await expect(preview).toHaveAttribute('data-preview', 'calm');
  await expect(page.getByRole('button', { name: 'Warm glow' })).toHaveAttribute('aria-pressed', 'true');
  await page.getByRole('button', { name: 'Soft pulse' }).click();
  await expect(preview).toHaveAttribute('data-preview', 'motion');
  await page.getByRole('button', { name: 'Color flow' }).click();
  await expect(preview).toHaveAttribute('data-preview', 'color');
  await expect(page.getByText(/not a connection to your lights/)).toBeVisible();
});

test('documentation prioritizes customer setup and leaves technical reference collapsed', async ({ page }) => {
  await page.goto('/docs');
  await expect(page.getByRole('heading', { name: 'LED Node guides' })).toBeVisible();
  await expect(page.getByRole('link', { name: /Start with LED Node/ })).toHaveAttribute('href', '/start');
  await expect(page.locator('details.advanced')).not.toHaveAttribute('open', '');
  await page.goto('/docs/API_REFERENCE');
  await expect(page.getByRole('heading', { name: 'HTTP API reference' })).toBeVisible();
  const response = await page.goto('/docs/not-a-real-guide');
  expect(response?.status()).toBe(404);
});

test('five languages stay complete and mobile pages do not overflow', async ({ page }, testInfo) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto('/');
  const expected = [
    ['lv', 'Visas jūsu gaismas. Vienuviet.', 'Sāciet lietot LED Node'],
    ['ru', 'Весь ваш свет. В одном месте.', 'Начните работу с LED Node'],
    ['nl', 'Al je verlichting. Eén plek om te bedienen.', 'Aan de slag met LED Node'],
    ['de', 'Dein Licht. Alles an einem Ort.', 'Starte mit deinem LED Node'],
    ['en', 'Your lighting. One place to control it.', 'Start with your LED Node']
  ] as const;
  for (const [language, title, guideTitle] of expected) {
    await page.getByRole('combobox').selectOption(language);
    await expect(page.getByTestId('hero-title')).toHaveText(title);
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    await page.getByRole('link', { name: /Get started|Sākt lietošanu|Начать|Aan de slag|Jetzt starten/, exact: true }).first().click();
    await expect(page.getByRole('heading', { name: guideTitle })).toBeVisible();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
    await page.getByRole('link', { name: /Back to LED Node|Atpakaļ pie LED Node|Назад к LED Node|Terug naar LED Node|Zurück zu LED Node/ }).first().click();
  }
  await page.screenshot({ path: testInfo.outputPath('landing-product-mobile.png'), fullPage: true });
});

test('historical reports are labeled and installer page does not invent a release', async ({ page }) => {
  await page.goto('/docs/history/2026-10-21/LG-P0-BOOT-001_REPORT');
  await expect(page.getByText('Historical record.', { exact: false }).first()).toBeVisible();
  await page.goto('/flasher');
  await expect(page.getByRole('heading', { name: 'LED Node setup and updates' })).toBeVisible();
  await expect(page.locator('esp-web-install-button')).toHaveCount(0);
});
