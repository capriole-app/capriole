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

## Previste (da verificare all'aggiunta)

| Libreria        | Licenza attesa        | Fase | Uso |
|-----------------|-----------------------|------|-----|
| libjpeg-turbo   | IJG + BSD-3 + Zlib    | 6    | DCTDecode |
| OpenJPEG        | BSD-2-Clause          | 6    | JPXDecode |
| FreeType        | FTL (BSD-like)        | 6    | font — **scegliere FTL, non GPLv2** (doppia licenza) |
| HarfBuzz        | MIT ("Old MIT")       | 8    | shaping del testo inserito |
| Little CMS      | MIT                   | 6    | ICC |
| mbedTLS o OpenSSL 3 | Apache-2.0        | 8    | PAdES (X.509, CMS, RSA/ECDSA) — aggiunta alla spec §4.2 all'avvio |

Implementazione propria (nessuna libreria compatibile): JBIG2 (jbig2dec è AGPL), CCITT Fax,
LZW, RunLength, ASCIIHex/ASCII85, crittografia dei PDF (MD5, RC4, AES, SHA-2).

## Solo strumenti esterni di test (mai linkati)

MuPDF/mutool (AGPL), Poppler (GPL), PDFium (BSD, ma API da viewer), pdf.js (Apache-2.0), qpdf (Apache-2.0).

## Inspector app (`apps/inspector`) — npm, devDependencies only

Versions pinned by `apps/inspector/package-lock.json`; nothing is linked into the engine. All
permissive licences: `svelte`, `@sveltejs/kit`, `@sveltejs/adapter-static`, `@sveltejs/vite-plugin-svelte`,
`vite`, `tailwindcss`, `@tailwindcss/vite`, `mode-watcher`, `clsx`, `tailwind-merge`, `svelte-check`,
`prettier`, `prettier-plugin-svelte`, `prettier-plugin-tailwindcss` (MIT); `@lucide/svelte` (ISC);
`typescript` 6.x (Apache-2.0; 7.x is not yet accepted by Kit and svelte-check); `@playwright/test`
(Apache-2.0, tests only). `npm audit`: 3 *low* advisories in `cookie` via SvelteKit's server side, which
a static site does not have.
