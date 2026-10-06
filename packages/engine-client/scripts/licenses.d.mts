// The types of licenses.mjs (a Node module: the apps' type check reads this, not its code).

interface Context {
  environment?: { name: string };
  emitFile(file: { type: 'asset'; fileName: string; source: string }): string;
}

interface Output {
  type: string;
  modules?: Record<string, { renderedLength: number }>;
}

/** The Vite plugin that writes licenses.json and licenses.txt at build time (see licenses.mjs). */
export function licenses(options: { crates: boolean }): {
  name: string;
  apply: 'build';
  configResolved(config: { root: string }): void;
  generateBundle(this: Context, options: unknown, bundle: Record<string, Output>): void;
};
