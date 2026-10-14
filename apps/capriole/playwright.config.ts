import { defineConfig, devices } from '@playwright/test';
import { termsKey, termsVersion } from './src/lib/terms-version';

// End-to-end: the built app (static files) in a real browser, the engine in its WebAssembly worker.
// Port 4174: the Inspector's tests use 4173.
// CAPRIOLE_E2E_URL: the suite against an app served elsewhere (deploy/web/test.sh: the image as it runs
// on the server), with no server of its own.
const external = process.env.CAPRIOLE_E2E_URL;
const baseURL = external ?? 'http://localhost:4174';

export default defineConfig({
  testDir: 'e2e',
  fullyParallel: false,
  workers: 1,
  reporter: 'list',
  // Italian, the interface's source language (the tests read its words); e2e/i18n.test.ts sets others.
  // The terms of use already accepted: e2e/terms.test.ts starts without, to see them asked.
  use: {
    baseURL,
    trace: 'retain-on-failure',
    locale: 'it-IT',
    storageState: {
      cookies: [],
      origins: [{ origin: new URL(baseURL).origin, localStorage: [{ name: termsKey, value: termsVersion }] }],
    },
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: external
    ? undefined
    : {
        command: 'npx vite preview --port 4174 --strictPort',
        url: 'http://localhost:4174',
        reuseExistingServer: false,
      },
});
