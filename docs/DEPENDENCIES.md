# Dipendenze

Regola (spec §4.2): nel motore solo licenze permissive. Ogni dipendenza è qui **prima** di essere usata,
con licenza verificata alla fonte, e in `cmake/Dependencies.cmake` con versione e SHA-256.

## In uso

| Libreria    | Versione | Licenza      | Dove           | Note |
|-------------|----------|--------------|----------------|------|
| GoogleTest  | 1.17.0   | BSD-3-Clause | solo test      | FetchContent, hash fissato |
| zlib        | 1.3.1    | Zlib         | motore + test  | FetchContent, hash fissato; compilato da `cmake/Dependencies.cmake` come target proprio (`capriole::zlib`, niente CMake di zlib), stessa versione in nativo e WASM |

## Previste (da verificare all'aggiunta)

| Libreria        | Licenza attesa        | Fase | Uso |
|-----------------|-----------------------|------|-----|
| libjpeg-turbo   | IJG + BSD-3 + Zlib    | 6    | DCTDecode |
| OpenJPEG        | BSD-2-Clause          | 6    | JPXDecode |
| FreeType        | FTL (BSD-like)        | 6    | font — **scegliere FTL, non GPLv2** (doppia licenza) |
| HarfBuzz        | MIT ("Old MIT")       | 8    | shaping del testo inserito |
| Little CMS      | MIT                   | 6    | ICC |
| Skia / Blend2D  | BSD-3 / Zlib          | 6    | rasterizzazione — decisione aperta |
| mbedTLS o OpenSSL 3 | Apache-2.0        | 8    | PAdES (X.509, CMS, RSA/ECDSA) — aggiunta alla spec §4.2 all'avvio |

Implementazione propria (nessuna libreria compatibile): JBIG2 (jbig2dec è AGPL), CCITT Fax,
LZW, RunLength, ASCIIHex/ASCII85, crittografia dei PDF (MD5, RC4, AES, SHA-2).

## Solo strumenti esterni di test (mai linkati)

MuPDF/mutool (AGPL), Poppler (GPL), PDFium (BSD, ma API da viewer), pdf.js (Apache-2.0), qpdf (Apache-2.0).
