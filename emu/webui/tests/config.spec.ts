import { test, expect } from '@playwright/test';

// /config: o formulário posta em /config/save (302 de volta para /config).
// updateCounts() roda no load, então salvar só o nome preserva os servidores
// já configurados (essencial: o resto da suíte depende do raiznetd receber
// telemetria). A flash é recriada a cada execução do wrapper, então o nome
// alterado aqui não vaza para outras execuções.
test('formulário de config salva um nome novo e ele persiste', async ({ page }) => {
  await page.goto('/config');

  const nameInput = page.locator('input[name="device_name"]');
  await expect(nameInput).toBeVisible();

  await nameInput.fill('PlaywrightUI');
  await page.locator('form#f button[type="submit"]').click();

  // 302 → /config; o input volta preenchido com o valor salvo na NVS.
  await expect(page).toHaveURL(/\/config/);
  await expect(page.locator('input[name="device_name"]')).toHaveValue('PlaywrightUI');

  // Recarga completa para provar que veio da flash, não do estado do form.
  await page.reload();
  await expect(page.locator('input[name="device_name"]')).toHaveValue('PlaywrightUI');
});
