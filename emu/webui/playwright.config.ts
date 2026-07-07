import { defineConfig } from '@playwright/test';

// A stack (QEMU + raiznetd) é levantada pelo wrapper emu/webui-playwright.sh —
// não há webServer aqui. O device emulado é lento: timeouts generosos e um
// worker só (um único firmware atende todas as páginas).
export default defineConfig({
  testDir: './tests',
  fullyParallel: false,
  workers: 1,
  retries: 1,
  timeout: 90_000,
  expect: { timeout: 20_000 },
  reporter: 'list',
  use: {
    baseURL: 'http://127.0.0.1:8180',
    headless: true,
    navigationTimeout: 15_000,
    actionTimeout: 15_000,
  },
  projects: [{ name: 'chromium', use: { browserName: 'chromium' } }],
});
