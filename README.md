# Capriole

An open source PDF editor, built on its own PDF engine written from scratch in C++23.
Files never leave the user's device: the same engine runs natively and as WebAssembly in the browser.
Try it at **[capriole.app](https://capriole.app)**.

> **Status:** in development, not yet released. The engine reads, renders, extracts text from and edits
> PDF files; the web app and the desktop shell use it. See [docs/ROADMAP.md](docs/ROADMAP.md) for where
> things stand and [docs/PLAN.md](docs/PLAN.md) for the detail (the project's documentation is in Italian;
> code, comments and these top-level files are in English).

## What it does

- **Engine** (`engine/`): a strict PDF parser with a separate repair layer, an incremental-save writer
  (encryption included), a content interpreter and renderer (Blend2D), text extraction with provenance.
- **Editing**: pages (reorder, rotate, delete, merge, extract), annotations, AcroForm filling, attachments
  and Factur-X invoices, true redaction, PAdES signatures, PDF/A conversion, editing a page's text.
- **Apps**: `apps/capriole`, the web app (Svelte 5, the engine as WebAssembly in a worker, everything
  client-side); `apps/desktop`, the same app in a Tauri shell; `apps/inspector`, a PDF inspector for
  debugging.
- **API**: a stable C API (`capi/`), the only boundary to WebAssembly, the desktop and third parties.

The engine treats every file as hostile: parsers have fuzz targets (`fuzz/`), and every fixed bug leaves
a regression test with its minimized file.

## Building

Requirements: CMake ≥ 3.28, Ninja, a C++23 compiler (GCC ≥ 13 or Clang ≥ 19). Optional: Clang for
fuzzing, [emsdk](https://emscripten.org) for the WebAssembly build, Node 22 for the apps, Rust for the
desktop shell. See [docs/SETUP.md](docs/SETUP.md).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

scripts/check.sh          # the gate: format, ASan/UBSan build + tests, clang-tidy
SKIP_WASM=0 SKIP_E2E=0 scripts/check.sh   # + WebAssembly, the apps and their end-to-end tests
```

| Preset    | What                                              |
|-----------|---------------------------------------------------|
| `debug`   | native debug build                                |
| `release` | native optimized build with debug info            |
| `asan`    | debug + AddressSanitizer/UBSan, warnings = errors |
| `fuzz`    | libFuzzer targets (Clang)                         |
| `wasm`    | WebAssembly module (Emscripten)                   |

`build/debug/tools/cpdf/cpdf` is the developer CLI: every engine capability has a subcommand.

## Layout

`engine/` the C++ library · `capi/` the stable C API · `bindings/wasm/` the WebAssembly module ·
`packages/engine-client/` the apps' engine client · `apps/` · `tools/cpdf/` developer CLI ·
`tests/` · `fuzz/` · `corpus-tools/` checks against other PDF engines · `docs/`.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) (contributions are signed off under the DCO) and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Report vulnerabilities privately: [SECURITY.md](SECURITY.md). Anything else:
[hello@capriole.app](mailto:hello@capriole.app).

## License

[0BSD](LICENSE) (BSD Zero Clause License): use, copy, modify and distribute it for any purpose, with or
without fee, with no conditions. Third-party components built into the engine keep their own licences:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) (generated) and [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md);
the apps list theirs in «Informazioni e licenze». Rationale: [ADR 0009](docs/adr/0009-licenza.md).
