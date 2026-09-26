# CLAUDE.md — capriole

Istruzioni per lavorare in questo repository. Specifica di prodotto: `docs/CAPRIOLE.md`.
Piano di lavoro: `docs/PLAN.md`. Decisioni: `docs/adr/`.

## Cos'è

Motore PDF scritto da zero in C++23 (parser, modello a oggetti, writer, renderer, editing) più,
in futuro, le app che lo usano: web (WASM, tutto client-side) e desktop. È un **monorepo** per scelta
(ADR 0001): il motore, l'API C e i binding devono restare allineati commit per commit.

## Struttura

```
engine/      libreria C++ (capriole::engine) — include/capriole/<area>/, src/<area>/
capi/        API C stabile: l'UNICO confine verso WASM, FFI desktop, terzi
bindings/wasm/  modulo Emscripten (ES module) + smoke test Node
tools/cpdf/  CLI di sviluppo: ogni capacità del motore ha prima un sottocomando qui
tests/unit/  GoogleTest     tests/regression/  un file minimizzato per bug   tests/differential/  vs motori esterni
fuzz/        target libFuzzer + corpus seme (fuzz/corpus/<target>/)
cmake/       moduli CMake     scripts/check.sh  IL GATE     scripts/hooks/  pre-push
ci/          immagine della CI (Jenkinsfile alla radice)     docs/  piano, setup, ADR, dipendenze
```

Aree previste in `engine/`: `core` (errori, ByteSource), `parser` (lexer, xref, oggetti — rigoroso),
`repair` (tolleranza, SEPARATO da parser), `model` (modello basso con stato modifiche), `writer`,
`filters`, `crypto`, `content` (interprete), `render`, `text`, `edit`.

## Comandi

```
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
scripts/check.sh              # il gate: format + ASan/UBSan + test + clang-tidy
SKIP_WASM=0 scripts/check.sh  # + build/test WebAssembly (serve: source ~/emsdk/emsdk_env.sh)
cmake --preset fuzz && cmake --build --preset fuzz   # richiede clang
./build/debug/tools/cpdf/cpdf header file.pdf
```

**Prima di dire che una modifica è pronta, `scripts/check.sh` deve essere verde.** L'hook `pre-push`
(`git config core.hooksPath scripts/hooks`) lo esegue a ogni push. La CI è il Jenkins del workspace
(`Jenkinsfile` + `ci/Dockerfile`), che invoca lo stesso script. Niente GitHub Actions (minuti a pagamento).

## Principi che NON si violano

1. **Input ostile.** Ogni byte del file è non fidato. Niente letture fuori dai limiti, niente ricorsione
   senza limite di profondità, niente allocazioni guidate da un numero letto dal file senza tetto
   (`ErrorCode::kLimitExceeded`). Ogni parser nuovo nasce con il suo target di fuzzing.
2. **Errori come valori.** Tutto ciò che può fallire restituisce `Result<T>` (`std::expected`).
   Niente eccezioni per errori di input; nulla attraversa l'API C senza essere catturato.
3. **Parser rigoroso e riparazione separati.** `parser/` segue la specifica e fallisce con un errore
   preciso; `repair/` interviene dopo. Mai tolleranze sparse dentro il parser (spec §7.1).
   Eccezione dichiarata: header entro 1024 byte (`parser/header.hpp`), tollerata da tutti i lettori.
4. **Toccare solo ciò che l'utente modifica.** Il modello basso è la fonte di verità, ogni oggetto
   ha stato (non modificato/modificato/nuovo/eliminato); salvataggio preferito incrementale;
   aprire e salvare senza modifiche non cambia il significato del file (spec §5).
5. **Mai presupporre il file intero in memoria.** Si legge tramite `ByteSource` (limite wasm32 4 GB).
6. **Determinismo.** Stesso file, stesso risultato: niente dipendenza da ordine di hash, orari, thread.
7. **Licenze.** Nessuna dipendenza copyleft linkata. Ogni dipendenza nuova va in
   `cmake/Dependencies.cmake` (versione + SHA-256) e in `docs/DEPENDENCIES.md` PRIMA di usarla.
   MuPDF, PDFium, pdf.js, Poppler, qpdf sono solo strumenti di test esterni, mai linkati.
   Non copiare codice da quei progetti.
8. **Ogni bug corretto lascia un test di regressione** con il file minimizzato.

## Stile

- `.clang-format` (Google, 2 spazi, 160 colonne) e `.clang-tidy` sono la legge; il gate li verifica.
- Tipi `PascalCase`, funzioni/variabili `snake_case`, membri privati `snake_case_`, costanti `kPascalCase`,
  namespace `capriole::<area>`. API C: `capriole_*`, C puro.
- Commenti e identificatori in inglese; documentazione di progetto in italiano.
- Commenti che spiegano il **perché** (riferimento alla sezione ISO 32000-2 quando pertinente).

## Git

Si lavora su **`development`** (o branch `feat/…`, `fix/…` che confluiscono in `development`).
`main` si aggiorna solo per merge da `development`; l'hook blocca il push diretto. Conventional Commits.
Remote: `github.com/capriole-app/capriole` (privato).
