# Capriole

An open source PDF editor, built on its own PDF engine written in C++.
Files never leave the user's device: the same engine runs natively on the desktop and as
WebAssembly in the browser.

> **Status:** early development — the engine skeleton (phase 1 of [docs/PLAN.md](docs/PLAN.md)).
> Nothing here is usable yet.

## Building

Requirements: CMake ≥ 3.28, Ninja, a C++23 compiler (GCC ≥ 13 or Clang ≥ 18).
Optional: Clang for fuzzing, [emsdk](https://emscripten.org) for the WebAssembly build.
See [docs/SETUP.md](docs/SETUP.md).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

scripts/check.sh          # full gate: format, ASan/UBSan build + tests, clang-tidy
```

| Preset    | What                                             |
|-----------|--------------------------------------------------|
| `debug`   | native debug build                               |
| `release` | native optimized build with debug info           |
| `asan`    | debug + AddressSanitizer/UBSan, warnings = errors |
| `fuzz`    | libFuzzer targets (Clang)                        |
| `wasm`    | WebAssembly module (Emscripten)                  |

## Layout

`engine/` the C++ library · `capi/` the stable C API · `bindings/wasm/` the WebAssembly module ·
`tools/cpdf/` developer CLI · `tests/` · `fuzz/` · `docs/`.

## License

Not decided yet. Until it is, all rights are reserved and external contributions cannot be accepted.
