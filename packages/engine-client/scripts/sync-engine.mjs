// Copies the engine's WebAssembly module (capriole.mjs + capriole.wasm) from the wasm build into this
// package (src/generated), for the app given as the first argument (default: the working directory).
// Build it first from the repository root:
//   source ~/emsdk/emsdk_env.sh && cmake --preset wasm && cmake --build --preset wasm
// CAPRIOLE_WASM_DIR overrides where the module is looked for.
//
// Also copies the resource pack (fonts, CMaps and the CMYK press profile the engine fetches when a page
// needs them, ADR 0005 and 0006), when it has been fetched — scripts/fetch_resources.sh, into
// build/resources; CAPRIOLE_RESOURCES_DIR overrides — to static/resources, every directory of it (a part
// added to the pack is served without changing this script). Without it the app works: CJK text that is
// not embedded is not drawn, DeviceCMYK uses the engine's built-in table.
import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const packageDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const app = resolve(process.argv[2] ?? process.cwd());
const from = process.env.CAPRIOLE_WASM_DIR ?? join(packageDir, '../../build/wasm/bindings/wasm');
const to = join(packageDir, 'src/generated');

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
copyFileSync(join(packageDir, 'scripts/capriole.d.mts'), join(to, 'capriole.d.mts'));
console.log(`engine: ${from} -> packages/engine-client/src/generated`);

const pack = process.env.CAPRIOLE_RESOURCES_DIR ?? join(packageDir, '../../build/resources');
if (existsSync(join(pack, 'cmap')) && existsSync(join(pack, 'font'))) {
  // Hidden directories are the fetch script's own (.downloads: the archives it unpacked), not the pack.
  const parts = readdirSync(pack, { withFileTypes: true }).filter(
    (entry) => entry.isDirectory() && !entry.name.startsWith('.'),
  );
  for (const part of parts) {
    cpSync(join(pack, part.name), join(app, 'static/resources', part.name), { recursive: true });
  }
  console.log(`resources: ${pack} -> static/resources (${parts.map((part) => part.name).join(', ')})`);
} else {
  console.log('resources: no resource pack (scripts/fetch_resources.sh), not copied');
}
