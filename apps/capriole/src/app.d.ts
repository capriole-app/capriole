// See https://svelte.dev/docs/kit/types#app.d.ts
declare global {
  namespace App {}
  /** The app's version (package.json), put in by Vite (vite.config.ts). */
  const __APP_VERSION__: string;
}

export {};
