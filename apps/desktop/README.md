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
files), and the shell's own four commands (PLAN.md phase 11c, `src-tauri/src/`): `pkcs11_tokens`,
`pkcs11_certificates`, `pkcs11_sign` — a smart card through its PKCS#11 module (`cryptoki`): the key
never leaves the card, the PIN never leaves the shell — and `net_fetch`, the requests of a long-term
signature (a time stamp, an OCSP answer, a CRL, an issuer's certificate: http and https only, a size
cap, a time limit), made from Rust because the engine makes no network calls and many of those
services send no CORS headers. The content security policy (`src-tauri/tauri.conf.json`) allows the
page's own scripts, WebAssembly and its workers, nothing from the network: the page reaches it only
through `net_fetch`.

Tests: `cargo test` (the IPC with the app's capabilities in a mock runtime, the network against a local
server), and `scripts/test_desktop_pkcs11.sh` for the card against SoftHSM.
