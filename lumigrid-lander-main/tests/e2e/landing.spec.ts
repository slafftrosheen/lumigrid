import { test, expect } from '@playwright/test';

test('hero renders and CTAs are visible', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('hero-title')).toBeVisible();
  await expect(page.getByRole('link', { name: /see examples/i })).toBeVisible();
  await expect(page.getByRole('link', { name: /talk to us/i })).toBeVisible();
});

test('process steps are numbered and visible', async ({ page }) => {
  await page.goto('/');
  const howItWorksSection = page.getByTestId('how-it-works');
  await expect(howItWorksSection).toBeVisible();
  await expect(howItWorksSection.getByText(/brief/i)).toBeVisible();
  await expect(howItWorksSection.getByText(/plan/i)).toBeVisible();
  await expect(howItWorksSection.getByText(/install/i)).toBeVisible();
  await expect(howItWorksSection.getByText(/support/i)).toBeVisible();
});

test('feature grid shows all sections', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('feature-grid-title')).toBeVisible();
  await expect(page.getByTestId('feature-title-1')).toBeVisible();
  await expect(page.getByTestId('feature-title-2')).toBeVisible();
  await expect(page.getByTestId('feature-title-3')).toBeVisible();
  await expect(page.getByTestId('feature-title-4')).toBeVisible();
});
