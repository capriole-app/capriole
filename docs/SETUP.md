# Ambiente di sviluppo

Riferimento: Ubuntu 24.04. Le versioni minime sono quelle verificate dal gate e dalla CI.

## Pacchetti di sistema

```sh
sudo apt install cmake ninja-build clang-18 clang-tidy-18 libclang-rt-18-dev lld-18 \
                 gdb ccache pkg-config \
                 qpdf mupdf-tools poppler-utils
```

- `cmake` (3.28), `ninja-build`: build.
- `clang-18`, `libclang-rt-18-dev`: libFuzzer e sanitizer con Clang (preset `fuzz`).
- `clang-tidy-18`: analisi statica nel gate. Ubuntu installa i binari con suffisso; se `clang-tidy`
  non è nel PATH: `sudo update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-18 100`
  (idem `clang`/`clang++` se servono senza suffisso).
- `qpdf`, `mupdf-tools` (`mutool`), `poppler-utils` (`pdftoppm`, `pdfinfo`): **motori di riferimento
  per i test differenziali**, usati come programmi esterni. Non si linkano mai (licenze, spec §7.3).
  PDFium e pdf.js arrivano alla fase 4 (binario precompilato / Node).

## Emscripten (build WebAssembly)

Installato in user space, senza sudo:

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 6.0.10 && ~/emsdk/emsdk activate 6.0.10
source ~/emsdk/emsdk_env.sh       # in ogni shell che builda wasm
cmake --preset wasm && cmake --build --preset wasm && ctest --preset wasm
```

La versione usata è fissata in `.github/workflows/ci.yml` (`EMSDK_VERSION`): locale e CI devono coincidere.

## Corpora di test

I corpora (SafeDocs, UNSAFE-DOCS, file dai bug tracker) **non stanno nel repo**: si indica la cartella
con `CAPRIOLE_CORPUS_DIR` (default suggerito `~/corpora/capriole`). Vanno trattati come input ostile:
non si aprono con visualizzatori di sistema, si processano solo con i nostri strumenti.

## Editor

Il preset scrive `build/<preset>/compile_commands.json`; per clangd/VS Code puntare a `build/debug`.
