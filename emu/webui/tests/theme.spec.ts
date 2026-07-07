import { test, expect } from '@playwright/test';

// Toggle de tema (#themeBtn): alterna o atributo data-theme do <html> e
// persiste em localStorage. Contexto novo do Playwright usa colorScheme
// light, então o tema inicial é 'light'.
test('toggle de tema alterna data-theme e persiste após reload', async ({ page }) => {
  await page.goto('/');

  const html = page.locator('html');
  await expect(html).toHaveAttribute('data-theme', 'light');

  await page.locator('#themeBtn').click();
  await expect(html).toHaveAttribute('data-theme', 'dark');

  await page.reload();
  await expect(html).toHaveAttribute('data-theme', 'dark');

  // Volta para light para não depender de ordem entre specs.
  await page.locator('#themeBtn').click();
  await expect(html).toHaveAttribute('data-theme', 'light');
});
