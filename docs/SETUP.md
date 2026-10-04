# Ambiente di sviluppo

Riferimento: Ubuntu 24.04. Le versioni minime sono quelle verificate dal gate e dalla CI.

## Pacchetti di sistema

```sh
sudo apt install cmake ninja-build clang-20 clang-tidy-20 libclang-rt-20-dev lld-20 clang-format-18 \
                 gdb ccache pkg-config \
                 qpdf mupdf-tools poppler-utils
```

- `cmake` (3.28), `ninja-build`: build.
- **Clang ≥ 19 obbligatorio** (usiamo 20, nei repository ufficiali di Ubuntu 24.04). Clang 18 non vede
  `std::expected` negli header di libstdc++ 13, che lo abilitano solo con `__cpp_concepts >= 202002`,
  dichiarato da Clang solo dalla 19. Vale per `clang-tidy` e per il preset `fuzz`.
- `clang-20`, `libclang-rt-20-dev`: libFuzzer e sanitizer (preset `fuzz`, che usa `clang-20` per nome).
- `clang-tidy-20`: analisi statica nel gate; `scripts/check.sh` lo trova da solo anche con il suffisso.
- `clang-format-18`: formattazione (il gate e la CI usano la 18; cambiarla solo in entrambi i posti).
- Non servono alias senza suffisso (`update-alternatives`): preset e script usano i nomi versionati.
- `qpdf`, `mupdf-tools` (`mutool`), `poppler-utils` (`pdftoppm`, `pdfinfo`): **motori di riferimento
  per i test differenziali**, usati come programmi esterni. Non si linkano mai (licenze, spec §7.3).
  PDFium e pdf.js arrivano alla fase 4 (binario precompilato / Node).
- Facoltativi, per la verifica di Factur-X (`corpus-tools/check_attachments.py`), fuori dal repo:
  `python3 -m venv ../CORPORA/tools/venv && ../CORPORA/tools/venv/bin/pip install factur-x`
  (opzione `--facturx-python ../CORPORA/tools/venv/bin/python`), e veraPDF (Java 11+), installato in
  `../CORPORA/tools/verapdf` dal suo installer (opzione `--verapdf ../CORPORA/tools/verapdf/verapdf`).

## Node, per le app (`apps/inspector`, `apps/capriole`, `apps/desktop`)

Node **22** (fissato in `.nvmrc`, `engines` e `engine-strict`): per esempio con nvm, `nvm install 22`.
Quello incluso in emsdk è il 24 e non va bene per l'app. Per i test end-to-end, una volta:
`cd apps/inspector && npx playwright install chromium`. Istruzioni d'uso in `apps/inspector/README.md`.

## Rust e WebKitGTK, per la shell desktop (`apps/desktop`, fase 8c)

```sh
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y     # Rust stabile (rustup)
sudo apt update && sudo apt install libwebkit2gtk-4.1-dev build-essential libxdo-dev libssl-dev \
  libayatana-appindicator3-dev librsvg2-dev
cd apps/desktop && npm ci && npm run dev     # l'app nella shell (avvia anche apps/capriole in sviluppo)
npm run build                                # pacchetti .deb e AppImage in src-tauri/target/release/bundle/
```

Nel gate il passo desktop è facoltativo: `SKIP_WASM=0 SKIP_DESKTOP=0 scripts/check.sh` (clippy sul crate,
avvisi come errori). Compila con al più 4 job (`CARGO_BUILD_JOBS=4`): la macchina ha poca memoria.

## Hook git (obbligatorio, una volta per clone)

```sh
git config core.hooksPath scripts/hooks
```

`scripts/hooks/pre-push` è il gate: blocca il push diretto su `main` ed esegue `scripts/check.sh`,
build WebAssembly compresa se `~/emsdk` è installato. La CI gira sul nostro Jenkins (`Jenkinsfile`,
immagine `ci/Dockerfile`) e ripete lo stesso gate, più build release con Clang e fuzz smoke.

## Emscripten (build WebAssembly)

Installato in user space, senza sudo:

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 6.0.10 && ~/emsdk/emsdk activate 6.0.10
source ~/emsdk/emsdk_env.sh       # in ogni shell che builda wasm
cmake --preset wasm && cmake --build --preset wasm && ctest --preset wasm
```

La versione è fissata anche in `ci/Dockerfile` (`EMSDK_VERSION`): locale e CI devono coincidere.

## Corpora di test

I corpora (SafeDocs, UNSAFE-DOCS, file dai bug tracker) **non stanno nel repo**: stanno in `CORPORA/`
nella cartella contenitore (`../CORPORA` dal repo), oppure dove indica `CAPRIOLE_CORPUS_DIR`.
Si scaricano con `corpus-tools/fetch_corpus.sh` e si analizzano con `corpus-tools/run_corpus.py`
(vedi `corpus-tools/README.md`). Vanno trattati come input ostile:
non si aprono con visualizzatori di sistema, si processano solo con i nostri strumenti.

## Fuzzing lungo

```sh
scripts/fuzz_local.sh start 24     # campagna di 24 h in background, bassa intensità
scripts/fuzz_local.sh status       # avanzamento e problemi trovati finora
scripts/fuzz_local.sh stop         # interrompe (il corpus accumulato resta)
scripts/fuzz_local.sh report       # riepilogo dell'ultima campagna
```

Gira come unità systemd utente (`capriole-fuzz`) con limiti rigidi: 1 core, 1,5 GB senza swap, nice 19,
I/O idle, un fuzzer alla volta. Usa una sua copia del repo (`.git/capriole-fuzz-worktree`) ferma al commit
di avvio. Si ferma al logout, salvo `loginctl enable-linger`.

## Editor

Il preset scrive `build/<preset>/compile_commands.json`; per clangd/VS Code puntare a `build/debug`.
