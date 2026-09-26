# ADR 0001 — Fondamenta del repository

Data: 2026-09-26 · Stato: accettata

## Decisioni

1. **Monorepo** `capriole/` dentro il contenitore di progetto (che resta non versionato, come da
   convenzione del workspace). Motore, API C, binding WASM e in futuro le app vivono insieme perché il
   confine tra motore e UI deve cambiare in un solo commit.
2. **C++23** (non C++20 come nella bozza della spec): `std::expected` per gli errori senza eccezioni.
   Minimo: GCC 13, **Clang 19** (Clang 18 non vede `std::expected` in libstdc++ 13; si usa Clang 20),
   Emscripten 6.0.10. `<print>` non si usa finché il minimo è GCC 13.
3. **CMake ≥ 3.28 + preset** (`debug`, `release`, `asan`, `fuzz`, `wasm`), generatore Ninja.
4. **Dipendenze via FetchContent con versione e SHA-256 fissati**; nessun package manager per ora.
   Da rivalutare (vcpkg) quando arrivano le librerie di immagini e font, che servono anche in WASM.
5. **GoogleTest** per i test, **libFuzzer** (Clang) per il fuzzing, ASan+UBSan nel gate.
6. **API C stabile (`capi/`) come unico confine esterno.** WASM e shell desktop parlano C, così la
   scelta della UI (Tauri o altro) non vincola il motore.
7. **`ByteSource`** come astrazione di input ad accesso casuale: il motore non presuppone il file in memoria.
8. **Errori**: `Result<T> = std::expected<T, Error>`; `Error` porta codice, messaggio e offset nel file.
9. **CI su GitHub Actions** (il repo è su GitHub), non Jenkins: nessuno stack da avviare, solo build e test.

## Conseguenze

- Il gate locale è `scripts/check.sh`; la CI esegue gli stessi passi più release Clang, WASM e fuzz smoke.
- Senza licenza decisa il repo resta privato e non accetta contributi esterni; OSS-Fuzz richiederà la licenza.
