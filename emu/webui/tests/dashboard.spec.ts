import { test, expect } from '@playwright/test';

// Dashboard (/): o dashboard.js busca /api/status e /api/telemetry e preenche
// os cards de métrica. No QEMU os sensores são sintéticos e determinísticos:
// ec 1412 ppm, temp 24.5 °C, hum 61.2 %. A primeira leitura local pode levar
// alguns ciclos após o boot — timeout largo no primeiro assert.
test('dashboard carrega e mostra os valores sintéticos do QEMU', async ({ page }) => {
  await page.goto('/');

  await expect(page.locator('#deviceName')).toBeVisible();

  await expect(page.locator('#mEc [data-value]')).toHaveText('1412', { timeout: 45_000 });
  await expect(page.locator('#mTemp [data-value]')).toHaveText('24.5');
  await expect(page.locator('#mHum [data-value]')).toHaveText('61.2');
});

test('status de Wi-Fi, servidor e buffer visíveis', async ({ page }) => {
  await page.goto('/');

  await expect(page.locator('#wifiPill')).toBeVisible();
  await expect(page.locator('#serverPill')).toBeVisible();
  await expect(page.locator('#bufferPill')).toBeVisible();

  // A lista de servidores externos (raiznetd em 10.0.2.2) é renderizada
  // pelo JS a partir de /api/status como chips .server-chip.
  await expect(page.locator('#externalServers .server-chip').first())
    .toBeVisible({ timeout: 30_000 });
});
