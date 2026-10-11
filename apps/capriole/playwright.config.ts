import { defineConfig, devices } from '@playwright/test';

// End-to-end: the built app (static files) in a real browser, the engine in its WebAssembly worker.
// Port 4174: the Inspector's tests use 4173.
// CAPRIOLE_E2E_URL: the suite against an app served elsewhere (deploy/web/test.sh: the image as it runs
// on the server), with no server of its own.
const external = process.env.CAPRIOLE_E2E_URL;

export default defineConfig({
  testDir: 'e2e',
  fullyParallel: false,
  workers: 1,
  reporter: 'list',
  // Italian, the interface's source language (the tests read its words); e2e/i18n.test.ts sets others.
  use: { baseURL: external ?? 'http://localhost:4174', trace: 'retain-on-failure', locale: 'it-IT' },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: external
    ? undefined
    : {
        command: 'npx vite preview --port 4174 --strictPort',
        url: 'http://localhost:4174',
        reuseExistingServer: false,
      },
});
