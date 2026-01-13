import { test, expect } from '@playwright/test';

test('hero renders and CTAs are visible', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('hero-title')).toBeVisible();
  await expect(page.getByRole('link', { name: /see examples/i })).toBeVisible();
  await expect(page.getByRole('link', { name: /talk to us/i })).toBeVisible();
});

test('process steps are numbered and visible', async ({ page }) => {
  await page.goto('/');
  // Process section is #process, not how-it-works (which is stories)
  const processSection = page.locator('#process');
  await expect(processSection).toBeVisible();

  // Use regex to be flexible about case, but 'Brief', 'Plan', etc are in the process section
  // We use .first() because the description might also contain the word
  await expect(processSection.getByText(/brief/i).first()).toBeVisible();
  await expect(processSection.getByText(/plan/i).first()).toBeVisible();
  await expect(processSection.getByText(/install/i).first()).toBeVisible();
  await expect(processSection.getByText(/support/i).first()).toBeVisible();
});

test('feature grid shows all sections', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('feature-grid-title')).toBeVisible();
  await expect(page.getByTestId('feature-title-1')).toBeVisible();
  await expect(page.getByTestId('feature-title-2')).toBeVisible();
  await expect(page.getByTestId('feature-title-3')).toBeVisible();
  await expect(page.getByTestId('feature-title-4')).toBeVisible();
});
