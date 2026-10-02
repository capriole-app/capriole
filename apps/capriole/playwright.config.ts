import { defineConfig, devices } from '@playwright/test';

// End-to-end: the built app (static files) in a real browser, the engine in its WebAssembly worker.
// Port 4174: the Inspector's tests use 4173.
export default defineConfig({
  testDir: 'e2e',
  fullyParallel: false,
  workers: 1,
  reporter: 'list',
  use: { baseURL: 'http://localhost:4174', trace: 'retain-on-failure' },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: {
    command: 'npx vite preview --port 4174 --strictPort',
    url: 'http://localhost:4174',
    reuseExistingServer: false,
  },
});
