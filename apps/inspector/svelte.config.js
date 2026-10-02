import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
  preprocess: vitePreprocess(),
  compilerOptions: { runes: true },
  kit: {
    // Static files only (DOCS/CAPRIOLE.md §2): the whole app runs in the browser, no server.
    adapter: adapter(),
    // The engine client shared by the apps (packages/engine-client, ADR 0008).
    alias: { '@capriole/engine-client': '../../packages/engine-client/src' },
  },
};

export default config;
