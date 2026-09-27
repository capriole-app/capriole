// Copies the engine's WebAssembly module (capriole.mjs + capriole.wasm) from the wasm build into the
// app. Build it first from the repository root:
//   source ~/emsdk/emsdk_env.sh && cmake --preset wasm && cmake --build --preset wasm
// CAPRIOLE_WASM_DIR overrides where the module is looked for.
import { copyFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const from = process.env.CAPRIOLE_WASM_DIR ?? join(app, '../../build/wasm/bindings/wasm');
const to = join(app, 'src/lib/engine/generated');

for (const file of ['capriole.mjs', 'capriole.wasm']) {
  if (!existsSync(join(from, file))) {
    console.error(`engine not built: ${join(from, file)} is missing.`);
    console.error('Build it from the repository root: cmake --preset wasm && cmake --build --preset wasm');
    process.exit(1);
  }
}
mkdirSync(to, { recursive: true });
for (const file of ['capriole.mjs', 'capriole.wasm']) {
  copyFileSync(join(from, file), join(to, file));
}
// Its types: the generated JavaScript is not type-checked, the declared API is.
copyFileSync(join(app, 'scripts/capriole.d.mts'), join(to, 'capriole.d.mts'));
console.log(`engine: ${from} -> src/lib/engine/generated`);
