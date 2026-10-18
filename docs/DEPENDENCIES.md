# Dipendenze

Regola (spec §4.2): nel motore solo licenze permissive. Ogni dipendenza è qui **prima** di essere usata,
con licenza verificata alla fonte, e in `cmake/Dependencies.cmake` con versione e SHA-256.

## In uso

| Libreria    | Versione | Licenza      | Dove           | Note |
|-------------|----------|--------------|----------------|------|
| GoogleTest  | 1.17.0   | BSD-3-Clause | solo test      | FetchContent, hash fissato |
| zlib        | 1.3.1    | Zlib         | motore + test  | FetchContent, hash fissato; compilato da `cmake/Dependencies.cmake` come target proprio (`capriole::zlib`, niente CMake di zlib), stessa versione in nativo e WASM |
| Blend2D     | commit `58ca9460b413` (2026-09-05) | Zlib | motore (rendering) | FetchContent, hash fissato; statico; JIT solo nelle build native di rilascio (ADR 0002) |
| AsmJit      | commit `0d7f01052f2d` (2026-09-22) | Zlib | dentro Blend2D | FetchContent, hash fissato; compilato da Blend2D (JIT delle pipeline) |
| libjpeg-turbo | 3.2.0 (2026-06-30) | IJG + BSD-3-Clause | motore (DCTDecode) | ExternalProject (l'upstream rifiuta `add_subdirectory`), hash fissato; firma GPG del tarball verificata (chiave del progetto `0338 C8D8 D9FD A62C F9C4 21BD 7EC2 DBB6 F4DB F434`); statico, senza SIMD (il codice SIMD, zlib-licensed, non è compilato), solo l'API libjpeg a 8 bit (ADR 0003) |
| OpenJPEG | 2.5.4 | BSD-2-Clause | motore (JPXDecode) | ExternalProject come libjpeg-turbo, hash fissato (SHA-256 del tarball del tag ufficiale: nessuna firma pubblicata); statico, una sola thread (`OPJ_USE_THREAD=OFF`), niente strumenti né test; con clang+sanitizer `-fsanitize=function` spento solo per la libreria (ADR 0004) |
| FreeType | 2.14.3 (2026-03-22) | FTL (BSD-style; scelta al posto della GPLv2) | motore (contorni dei glifi) | FetchContent, hash fissato; firma GPG del tarball verificata (chiave di Werner Lemberg `E306 7470 7856 409F F194 8010 BE6C 3AAC 63AD 8E3F`); compilato dai sorgenti con i soli moduli sfnt, TrueType, Type 1, CFF, CID, psaux, psnames e le nostre opzioni (`cmake/freetype/`): niente rasterizzatori, hinting, interprete di bytecode, bitmap, SVG, compressione; con i sanitizer `-fsanitize=nonnull-attribute` spento solo per FreeType (ADR 0005) |
| Little CMS | 2.19.1 | MIT | motore (profili ICC, CalRGB/CalGray, DeviceCMYK) | FetchContent, hash fissato; compilato dai sorgenti; contesto con budget di memoria per ogni trasformazione (ADR 0006) |
| Tabella CMYK → sRGB | da SWOP2006_Coated3v2 | colori campionati dal profilo IDEAlliance | motore (DeviceCMYK senza profilo) | generata da `scripts/gen_cmyk_table.py` in `engine/src/content/cmyk_table.cpp` (9⁴ punti), hash del profilo fissato nello script (ADR 0006) |
| Mbed TLS | 3.6.7 (LTS) | Apache-2.0 (doppia licenza Apache-2.0 o GPL-2.0+: scelta Apache-2.0) | motore (firme digitali: X.509, RSA, ECDSA, hash, cifratura delle chiavi PKCS#12) | FetchContent, hash fissato (verificato con quello della release); compilato dai sorgenti con la nostra configurazione `cmake/mbedtls/`: niente TLS, PSA, entropia od orologio della piattaforma; CMS e PKCS#12 sono del motore |
| Font Foxit (14) | pdfjs-dist 2.16.105 (npm) | BSD-3-Clause (PDFium Authors) | motore (sostituti dei font non incorporati) | FetchContent del pacchetto npm, hash fissato; solo i 14 file `standard_fonts/Foxit*.pfb` (CFF), incorporati nel motore da `cmake/EmbedFiles.cmake`; nulla del codice di pdf.js (ADR 0005) |
| Adobe Glyph List + AGLFN | commit `4036a9ca` (2019-10-31) | BSD-3-Clause (Adobe) | motore (nomi dei glifi → Unicode) | tabelle generate da `scripts/gen_glyph_tables.py` in `engine/src/font/glyph_tables.cpp`, hash dei file fissati nello script |

## Previste (da verificare all'aggiunta)

| Libreria        | Licenza attesa        | Fase | Uso |
|-----------------|-----------------------|------|-----|
| HarfBuzz        | MIT ("Old MIT")       | 8    | shaping del testo inserito |

Implementazione propria (nessuna libreria compatibile): JBIG2 (jbig2dec è AGPL), CCITT Fax,
LZW, RunLength, ASCIIHex/ASCII85, crittografia dei PDF (MD5, RC4, AES, SHA-2).

## Pacchetto di risorse (non linkato, fornito dall'host: ADR 0005)

Scaricato da `scripts/fetch_resources.sh` con hash fissati, registrato a run-time (`font::Resources`,
`capriole_resource_add`); le licenze vanno nella cartella `LICENSES/` del pacchetto.

| Dati | Versione | Licenza | Uso |
|------|----------|---------|-----|
| Adobe CMap resources | tag 20231115 | BSD-3-Clause | CMap predefinite delle collezioni CJK (Japan1, GB1, CNS1, Korea1, KR) |
| Noto Sans CJK | 2.004, sottoinsiemi JP/SC/TC/KR | OFL 1.1 | sostituti dei font CJK non incorporati |
| Liberation | 2.1.5 | OFL 1.1 | glifi che i sostituti Foxit non hanno |
| SWOP2006_Coated3v2.icc | registro ICC (IDEAlliance) | «used, embedded, exchanged, and shared without restriction»; non modificabile né vendibile | profilo di stampa del DeviceCMYK (ADR 0006), distribuito non modificato |

## Solo strumenti esterni di test (mai linkati)

MuPDF/mutool (AGPL), Poppler (GPL), PDFium (BSD, ma API da viewer), pdf.js (Apache-2.0), qpdf (Apache-2.0),
jbig2dec (AGPL: oracolo delle fixture JBIG2, `tests/data/jbig2/make_fixtures.py`), libtiff tramite Pillow
(generazione delle fixture CCITT e MMR). Per la verifica degli allegati e di Factur-X (fase 8f, facoltativi, fuori dal
repo in `CORPORA/tools/`): la libreria Python `factur-x` (BSD, con gli XSD Factur-X) e veraPDF (GPL/MPL,
Java); `lxml` (BSD) per leggere gli XMP nella verifica. Per la verifica della redaction (fase 8g):
`pikepdf` (MPL-2.0, su qpdf) per leggere i flussi di una pagina e i testi della sua struttura. Per le firme (fase
8h): `pdfsig` (Poppler, NSS), `openssl` (Apache-2.0: `cms -verify`, `ts -reply`/`-verify`, la PKI di prova),
`certutil` (NSS). Per PDF/A (fase 8i): veraPDF 1.30.2 (`CORPORA/tools/verapdf`, installato senza rete
dall'installer scaricato dall'utente). Per l'estensione dei sottoinsiemi dei font (fase 9c): fontconfig
(`fc-list`, `fc-query`: i font installati e i loro caratteri), LibreOffice (MPL-2.0, `--convert-to pdf`) e il
Chromium di Playwright (`page.pdf()`) per generare documenti con sottoinsiemi dei font installati
(`corpus-tools/make_font_docs.py`, in `CORPORA/generated/`); i font installati (DejaVu, Liberation, Noto…) sono
input di prova, mai nel repository. Per lo spostamento di ciò che segue un paragrafo (fase 9d): `pikepdf` (già sopra)
scrive la pagina originale traslata, che Capriole e Poppler disegnano come riferimento
(`corpus-tools/check_paragraph_move.py`). Per la firma con una chiave su smart card (fase 11a): SoftHSM2 (BSD,
`softhsm2-util` e `libsofthsm2.so`: una smart card PKCS#11 in software, creata per ogni giro) e `pkcs11-tool`
di OpenSC (LGPL, solo eseguito: firma sul token) — `corpus-tools/check_signatures.py --pkcs11`. Per la revoca e
le firme a lungo termine (fase 11b): pyHanko 0.37 con pyhanko-cli (MIT, in un venv fuori dal repo,
`CORPORA/tools/pyhanko-venv`: `adesverify` e la sonda `corpus-tools/pyhanko_probe.py`), `openssl ocsp` e
`openssl ca -gencrl` per il responder e la CRL di prova (`tests/data/signing/make_ltv_fixtures.sh`).


OSS-Fuzz (`ci/oss-fuzz/`, quando il progetto sarà iscritto): l'immagine `gcr.io/oss-fuzz-base/base-builder`
di Google con `ninja-build`, `zip` e CMake ≥ 3.28 da pip; strumenti di build dell'immagine, nulla di linkato.

## Apps (`apps/inspector`, `apps/capriole`) — npm, devDependencies only

Versions pinned by each app's `package-lock.json`; nothing is linked into the engine. The same stack in
both (ADR 0008). All
permissive licences: `svelte`, `@sveltejs/kit`, `@sveltejs/adapter-static`, `@sveltejs/vite-plugin-svelte`,
`vite`, `tailwindcss`, `@tailwindcss/vite`, `mode-watcher`, `clsx`, `tailwind-merge`, `svelte-check`,
`prettier`, `prettier-plugin-svelte`, `prettier-plugin-tailwindcss` (MIT); `@lucide/svelte` (ISC);
`typescript` 6.x (Apache-2.0; 7.x is not yet accepted by Kit and svelte-check); `@playwright/test`
(Apache-2.0, tests only). `npm audit`: 3 *low* advisories in `cookie` via SvelteKit's server side, which
a static site does not have.

`apps/capriole` also has the desktop shell's JavaScript APIs, loaded only inside it (dynamic imports):
`@tauri-apps/plugin-dialog` 2.8.1, `@tauri-apps/plugin-fs` 2.6.0, and `@tauri-apps/api` 2.12.1 (MIT OR
Apache-2.0: through them, and directly for the shell's own commands — smart cards, the network of a
long-term signature: PLAN.md 11c).

## Desktop shell (`apps/desktop`, PLAN.md 8c, ADR 0008)

| Component | Version | Licence | Use |
|---|---|---|---|
| `tauri` (Rust crate) | 2.x, pinned by `src-tauri/Cargo.lock` | MIT OR Apache-2.0 | the shell: a WebKitGTK webview around `apps/capriole` |
| `tauri-build` | 2.x | MIT OR Apache-2.0 | build script (capabilities, context) |
| `tauri-plugin-dialog`, `tauri-plugin-fs` | 2.x | MIT OR Apache-2.0 | system open/save dialogs; reading and writing the files picked in them |
| Tauri's transitive crates | pinned by `src-tauri/Cargo.lock` | mostly MIT/Apache-2.0; **MPL-2.0**: `cssparser`, `cssparser-macros`, `selectors`, `dtoa-short` (via `tauri-utils` → `dom_query`), `option-ext` (via `dirs`) | brought by Tauri itself (ADR 0008); MPL-2.0 is file-level copyleft: used unmodified, their source linked in the app's «Licenze» page (`scripts/gen_crate_notices.py` → `apps/desktop/crate-notices.json`) |
| `cryptoki` (+ `cryptoki-sys`) | 0.12.1 (0.5.0) | Apache-2.0 | PKCS#11 (PLAN.md 11c): the smart card's own module (OpenSC, the CNS or CIE middleware), loaded at run time with `libloading` (ISC): the shell lists tokens and certificates and has the card sign; `secrecy`, `zeroize` (Apache-2.0 OR MIT) keep the PIN |
| `ureq` (+ `ureq-proto`, `utf8-zero`) | 3.4.2 | MIT OR Apache-2.0 | HTTP from the shell only (11c): time stamps, OCSP, CRLs, issuers' certificates — the engine makes no network calls; TLS by `rustls` (Apache-2.0 OR ISC OR MIT) on `ring` (Apache-2.0 AND ISC), `rustls-webpki`, `untrusted` (ISC), `subtle` (BSD-3-Clause), with the root certificates of `webpki-roots` (CDLA-Permissive-2.0: Mozilla's list, permissive) |
| `@tauri-apps/cli` (npm) | 2.12.1 | MIT OR Apache-2.0 | `tauri dev` / `tauri build` (prebuilt binary) |

The crates' own dependencies (a few hundred, as for any Tauri app) are pinned by `Cargo.lock`; their
licences are permissive (MIT, Apache-2.0, BSD, ISC, Zlib, Unicode) — to be checked with `cargo deny`
when the app is distributed. System libraries on Linux: WebKitGTK 4.1, GTK 3, libsoup 3 (LGPL, linked
dynamically from the system, as every Tauri app on Linux does: not linked into the engine). On Windows:
WebView2, the system's (installed by its bootstrapper where missing), and the `windows`/`webview2-com`
crates (MIT OR Apache-2.0): `gen_crate_notices.py` lists the crates of both platforms.

Distribution only (PLAN.md 12b), never linked:

| Component | Version | Licence | Use |
|---|---|---|---|
| OpenSC | 0.27.1 (archive's SHA-256 in `packaging/flathub/app.capriole.Capriole.yml`) | LGPL-2.1 | built inside the Flatpak only, so a smart card works from the sandbox: a PKCS#11 module the shell loads at run time, when the person names it, as with the host's OpenSC; its source is the archive the manifest names |
| pcsc-lite | 2.5.3 (archive's SHA-256 in `packaging/flathub/app.capriole.Capriole.yml`) | BSD-3-Clause (the library) | built inside the Flatpak only, as the runtime lacks it: the PC/SC client library OpenSC is built against and talks to the host's `pcscd` with (`--socket=pcsc`); the daemon and the tools are cleaned up |
| cargo-xwin, xwin | 0.23.1 (`ci/desktop.Dockerfile`) | MIT OR Apache-2.0 | cross-build of the Windows installer; they fetch Microsoft's CRT and Windows SDK (Microsoft's licence: build only, not redistributed) |
| NSIS | Ubuntu 24.04's `nsis` | zlib/libpng (+ bzip2) | the Windows installer; its stub is in the `setup.exe` |

