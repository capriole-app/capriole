// Copies the engine's WebAssembly module (capriole.mjs + capriole.wasm) from the wasm build into the
// app. Build it first from the repository root:
//   source ~/emsdk/emsdk_env.sh && cmake --preset wasm && cmake --build --preset wasm
// CAPRIOLE_WASM_DIR overrides where the module is looked for.
//
// Also copies the resource pack (fonts and CMaps the engine fetches when a page needs them, ADR 0005),
// when it has been fetched — scripts/fetch_resources.sh, into build/resources; CAPRIOLE_RESOURCES_DIR
// overrides — to static/resources. Without it the app works; CJK text that is not embedded is not drawn.
import { copyFileSync, cpSync, existsSync, mkdirSync } from 'node:fs';
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

const pack = process.env.CAPRIOLE_RESOURCES_DIR ?? join(app, '../../build/resources');
if (existsSync(join(pack, 'cmap')) && existsSync(join(pack, 'font'))) {
  for (const part of ['cmap', 'font', 'LICENSES']) {
    if (existsSync(join(pack, part))) {
      cpSync(join(pack, part), join(app, 'static/resources', part), { recursive: true });
    }
  }
  console.log(`resources: ${pack} -> static/resources`);
} else {
  console.log('resources: no resource pack (scripts/fetch_resources.sh), not copied');
}
