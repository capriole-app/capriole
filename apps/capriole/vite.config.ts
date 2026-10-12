import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import { defineConfig } from 'vite';
import { licenses } from '../../packages/engine-client/scripts/licenses.mjs';
import packageJson from './package.json' with { type: 'json' };

export default defineConfig({
  plugins: [tailwindcss(), sveltekit(), licenses({ crates: true })],
  // The app's version, shown in «Informazioni».
  define: { __APP_VERSION__: JSON.stringify(packageJson.version) },
  // The engine runs in a module worker (packages/engine-client/src/worker.ts), so that parsing a large
  // file never freezes the page.
  worker: { format: 'es' },
  // The shared engine client lives outside the app.
  server: { fs: { allow: ['../../packages/engine-client'] } },
});
