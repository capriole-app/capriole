# Capriole

The web app (PLAN.md phase 8b): open a PDF and work on its pages — reorder them by dragging (or with the
toolbar's arrows), rotate, duplicate, delete, insert the pages of another PDF, extract the selected
pages into a new file, undo, save. Everything runs in the browser: the engine is the WebAssembly module
in a Web Worker (`packages/engine-client`), the file never leaves the device. A file that needed
repairs is repaired before editing (saved fully and read back in memory), and the app says so.

```sh
# once: the engine as WebAssembly (from the repository root)
source ~/emsdk/emsdk_env.sh && cmake --preset wasm && cmake --build --preset wasm
cd apps/capriole
npm ci
npm run dev        # http://localhost:5173 — copies the engine in first
npm run build      # static site in build/, to serve from anywhere
npm run check      # svelte-check (type-check)
npm run lint       # prettier --check
npx playwright test   # end-to-end in Chromium (port 4174)
```

Same stack as the Inspector (ADR 0008, the workspace's frontend standards). Keyboard: Ctrl/Cmd-click
adds a page to the selection, Shift-click a range, Ctrl/Cmd+A selects all, Delete deletes, Ctrl/Cmd+Z
undoes; the arrows in the toolbar move the selected pages. Undo keeps whole copies of the document
(up to 256 MB, the oldest dropped).

Next (phase 8c): the same app as the desktop shell (Tauri).
