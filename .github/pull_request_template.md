**What and why**

**Checklist**
- [ ] Branched from `development`; commits signed off (`git commit -s`, DCO)
- [ ] `scripts/check.sh` green (with `SKIP_WASM=0 SKIP_E2E=0` if the WebAssembly module or the apps changed)
- [ ] A fixed bug has its regression test; a new parser its fuzz target
- [ ] A new dependency is in `cmake/Dependencies.cmake` + `docs/DEPENDENCIES.md`, notices regenerated
