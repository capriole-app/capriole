import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import { defineConfig } from 'vite';

export default defineConfig({
  plugins: [tailwindcss(), sveltekit()],
  // The engine runs in a module worker (src/lib/engine/worker.ts), so that parsing a large file
  // never freezes the page.
  worker: { format: 'es' },
});
