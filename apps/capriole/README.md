# Capriole

The web app (PLAN.md phase 8b): open a PDF and work on its pages — reorder them by dragging (or with the
toolbar's arrows), rotate, duplicate, delete, insert the pages of another PDF, extract the selected
pages into a new file, undo, save. Everything runs in the browser: the engine is the WebAssembly module
in a Web Worker (`packages/engine-client`), the file never leaves the device. A file that needed
repairs is repaired before editing (saved fully and read back in memory), and the app says so.

Where files go: in browsers with the File System Access API (Chrome, Edge) "Scegli un PDF" and "Salva"
use the system's dialogs — "Salva" writes over the file opened (the browser asks once for permission),
or asks where for a file dropped in; "Salva con nome" always asks. In the others (Firefox, Safari) a
saved file is a download, where the browser puts downloads.

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

**Annotations** (phase 8d): double-click a page (or select it and press "Annota") to show it large.
Tools: highlight, underline and strike out (drag over the text: one mark per line), note (click; its
comment in the side panel), pen, rectangle, ellipse, line, free text (drag a box; its text in the
panel), and a colour for the next annotation. With "Seleziona e sposta", click an annotation to edit its
text or colour or remove it, drag it to move it; the panel also lists the page's annotations (the
keyboard's way: Delete removes the selected one, Escape lets it go, then goes back to the pages). Every
annotation gets its appearance from the engine, so other readers show it the same.

**Forms** (phase 8e): a page with form fields opens in the page view ready to fill them ("Compila il
modulo"): over each widget the control that fills it — text (one or several lines, password, maximum
length), check box, radio button, menu (with suggestions when the field takes any text), list (several
choices when the field allows). A value is written when the control is left, by the engine with its
appearance, so that every reader shows it; every value goes through undo. The side panel lists the
page's fields with their values (a click goes to the field). Signatures, push buttons and read-only
fields are shown by the page, not filled.

Same stack as the Inspector (ADR 0008, the workspace's frontend standards). Keyboard: Ctrl/Cmd-click
adds a page to the selection, Shift-click a range, Ctrl/Cmd+A selects all, Delete deletes, Ctrl/Cmd+Z
undoes; the arrows in the toolbar move the selected pages. Undo keeps whole copies of the document
(up to 256 MB, the oldest dropped).

Next (phase 8c): the same app as the desktop shell (Tauri).
