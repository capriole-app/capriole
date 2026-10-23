import { defineConfig, devices } from '@playwright/test';
import { termsKey, termsVersion } from './src/lib/terms-version';

// The landing's screenshots (shots/shots.spec.ts, npm run shots): the built app, served as for the e2e
// tests, the terms of use accepted. One scene after another: the Open Graph images use the text scenes.
const baseURL = 'http://localhost:4174';

export default defineConfig({
  testDir: 'shots',
  fullyParallel: false,
  workers: 1,
  reporter: 'list',
  timeout: 60_000,
  use: {
    baseURL,
    storageState: {
      cookies: [],
      origins: [{ origin: baseURL, localStorage: [{ name: termsKey, value: termsVersion }] }],
    },
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: { command: 'npx vite preview --port 4174 --strictPort', url: baseURL, reuseExistingServer: false },
});
