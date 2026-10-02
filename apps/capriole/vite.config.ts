import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import { defineConfig } from 'vite';

export default defineConfig({
  plugins: [tailwindcss(), sveltekit()],
  // The engine runs in a module worker (packages/engine-client/src/worker.ts), so that parsing a large
  // file never freezes the page.
  worker: { format: 'es' },
  // The shared engine client lives outside the app.
  server: { fs: { allow: ['../../packages/engine-client'] } },
});
