# Capriole Inspector

The engine at work in the browser: open a PDF and see what Capriole reads — version, encryption,
cross-reference sections, trailer, **the repairs it applied**, pages, every object (references are
links), stream data decoded or raw, each page drawn (vector or raster) with **its text over it** —
selectable in reading order, a click on a word shows the operator and the font it comes from and
highlights that operator in the page's content — and save it, fully or incrementally. Everything runs client-side:
the engine is the WebAssembly module (`bindings/wasm`) in a Web Worker, through the C API; the file
never leaves the browser.

A demo and a development tool now; later the debug panel of the real app (DOCS/CAPRIOLE.md §2: the
desktop app will be this same web UI inside Tauri).

```sh
# once: the engine as WebAssembly (from the repository root)
source ~/emsdk/emsdk_env.sh && cmake --preset wasm && cmake --build --preset wasm
cd apps/inspector
npm ci
npm run dev        # http://localhost:5173 — copies the engine in first (scripts/sync-engine.mjs)
npm run build      # static site in build/, to serve from anywhere
npm run check      # svelte-check (type-check)
npm run lint       # prettier --check
npx playwright test   # end-to-end in Chromium (npx playwright install chromium, once)
```

Stack (the workspace's frontend standards): Svelte 5 in runes mode, SvelteKit 2 with adapter-static
(no server), Vite 8, Tailwind 4 (tokens in `src/app.css`, no config file), `@lucide/svelte`,
`mode-watcher` (light / dark / system, applied before the first paint by the script in `app.html`).
Node 22, pinned in `.nvmrc`, `engines` and `.npmrc`. The gate (`scripts/check.sh` with `SKIP_WASM=0`)
formats, type-checks and builds it; `SKIP_E2E=0` also runs the browser tests.

Not yet: a Content-Security-Policy (the theme script would need its hash), editing.
