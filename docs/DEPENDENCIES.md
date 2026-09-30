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
| Font Foxit (14) | pdfjs-dist 2.16.105 (npm) | BSD-3-Clause (PDFium Authors) | motore (sostituti dei font non incorporati) | FetchContent del pacchetto npm, hash fissato; solo i 14 file `standard_fonts/Foxit*.pfb` (CFF), incorporati nel motore da `cmake/EmbedFiles.cmake`; nulla del codice di pdf.js (ADR 0005) |
| Adobe Glyph List + AGLFN | commit `4036a9ca` (2019-10-31) | BSD-3-Clause (Adobe) | motore (nomi dei glifi → Unicode) | tabelle generate da `scripts/gen_glyph_tables.py` in `engine/src/font/glyph_tables.cpp`, hash dei file fissati nello script |

## Previste (da verificare all'aggiunta)

| Libreria        | Licenza attesa        | Fase | Uso |
|-----------------|-----------------------|------|-----|
| HarfBuzz        | MIT ("Old MIT")       | 8    | shaping del testo inserito |
| mbedTLS o OpenSSL 3 | Apache-2.0        | 8    | PAdES (X.509, CMS, RSA/ECDSA) — aggiunta alla spec §4.2 all'avvio |

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
(generazione delle fixture CCITT e MMR).

## Inspector app (`apps/inspector`) — npm, devDependencies only

Versions pinned by `apps/inspector/package-lock.json`; nothing is linked into the engine. All
permissive licences: `svelte`, `@sveltejs/kit`, `@sveltejs/adapter-static`, `@sveltejs/vite-plugin-svelte`,
`vite`, `tailwindcss`, `@tailwindcss/vite`, `mode-watcher`, `clsx`, `tailwind-merge`, `svelte-check`,
`prettier`, `prettier-plugin-svelte`, `prettier-plugin-tailwindcss` (MIT); `@lucide/svelte` (ISC);
`typescript` 6.x (Apache-2.0; 7.x is not yet accepted by Kit and svelte-check); `@playwright/test`
(Apache-2.0, tests only). `npm audit`: 3 *low* advisories in `cookie` via SvelteKit's server side, which
a static site does not have.
