# @capriole/engine-client

The engine as the apps use it (ADR 0008): `EngineClient` posts requests to a module Web Worker that runs
the WebAssembly module (`bindings/wasm`) through the C API, one promise per request; `types.ts` is the
JSON the C API returns (`capi/README.md`). Shared by `apps/inspector` and `apps/capriole`.

Not an npm package: plain TypeScript sources the apps import through an alias in their
`svelte.config.js` (`kit.alias['@capriole/engine-client']`), with `server.fs.allow` reaching this folder.

`scripts/sync-engine.mjs [app dir]` copies the built module into `src/generated/` (with its declared
types, `scripts/capriole.d.mts`) and, when the resource pack has been fetched
(`scripts/fetch_resources.sh`), the pack into the app's `static/resources/`. Each app's `npm run engine`
calls it.
