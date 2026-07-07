import { test, expect } from '@playwright/test';

// Navegação entre as páginas pela tab bar (.local-tab). A aba ativa recebe a
// classe is-active no server-side render.
test('tabs navegam entre /, /config e /docs', async ({ page }) => {
  await page.goto('/');
  await expect(page.locator('.local-tab.is-active')).toHaveAttribute('href', '/');

  await page.locator('.local-tab[href="/config"]').click();
  await expect(page).toHaveURL(/\/config/);
  await expect(page.locator('.local-tab.is-active')).toHaveAttribute('href', '/config');

  await page.locator('.local-tab[href="/docs"]').click();
  await expect(page).toHaveURL(/\/docs/);
  await expect(page.locator('.local-tab.is-active')).toHaveAttribute('href', '/docs');

  await page.locator('.local-tab[href="/"]').click();
  await expect(page).toHaveURL(/\/(\?.*)?$/);
  await expect(page.locator('.local-tab.is-active')).toHaveAttribute('href', '/');
});

// /raiznet existe como rota mesmo com o item de menu oculto por default
// (#raiznet-menu-item aparece via JS quando há servidor local configurado).
test('/raiznet acessível por URL direta', async ({ page }) => {
  await page.goto('/raiznet');
  await expect(page.locator('.local-tab.is-active')).toHaveAttribute('href', '/raiznet');
});
