# Capriole desktop

The web app (`apps/capriole`) in a Tauri 2 shell (PLAN.md phase 8c, ADR 0008): the same page editor,
with the system's file dialogs — "Open" and "Insert PDF" use the open dialog, "Save" writes over the
file that was opened, "Save as" asks where. The engine is the same WebAssembly module, in the webview's
worker; files never leave the computer. The resource pack (CJK fonts, CMaps, the CMYK profile) is
bundled with the app when it has been fetched (`scripts/fetch_resources.sh`).

```sh
# once: Rust and WebKitGTK (docs/SETUP.md), the engine as WebAssembly, the web app's packages
cd apps/capriole && npm ci && cd ../desktop && npm ci
npm run dev        # the shell around `npm run dev` of apps/capriole (hot reload)
npm run build      # .deb and AppImage in src-tauri/target/release/bundle/
```

What the shell may do is in `src-tauri/capabilities/default.json`: the open and save dialogs, and
reading and writing the files the person picks in them (the fs plugin's scope is exactly those
files). The content security policy (`src-tauri/tauri.conf.json`) allows the page's own scripts,
WebAssembly and its workers, nothing from the network.
