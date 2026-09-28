# ADR 0002 — Rasterizzatore: Skia o Blend2D

Data: 2026-09-27 · Stato: **accettata** (2026-09-28)

## Contesto

La fase 6 disegna le pagine. L'interprete (6b) produce una lista di comandi indipendente dal
rasterizzatore; serve una libreria che la trasformi in pixel. Candidati (PLAN.md): **Skia** (Google,
BSD-3) e **Blend2D** (zlib, con AsmJit per le pipeline JIT). Il vincolo che pesa di più è la **web app
client-side** (CAPRIOLE.md §2): il motore gira in WebAssembly nel browser, e con Tauri anche il desktop
userà la stessa UI — quindi velocità e peso **in WASM** contano più di quelli nativi.

## Prototipo (`experiments/raster/`, fuori dal motore e dal gate)

Un'interfaccia `RasterBackend`, due implementazioni sulla **stessa** lista di comandi, regole comuni
(linee sotto il pixel a 1 px come Poppler, testo come riquadri dei glifi, immagini come segnaposto).
Skia: build precompilate JetBrains skia-pack m144 (Linux x64, WASM). Blend2D: sorgenti al commit
`58ca9460b413` + AsmJit `0d7f01052f2d`, compilati con CMake (in WASM senza JIT). Sorgenti in
`../THIRD_PARTY/`, fuori dal repository. Misure a 150 dpi, una sola thread, il migliore di 2 run.

### Fedeltà (pagine vettoriali sintetiche, contro pdftoppm)

| Pagina | Skia: pixel diversi > 32 | Blend2D |
|---|---|---|
| spessori, estremità, giunzioni, linee sottili | 0,48% | 0,45% |
| trattini, curve, even-odd | 0,20% | 0,43% (**senza trattini**, vedi sotto) |
| clip annidati (tracciati, rettangoli, even-odd) | 0,33% | 0,34% |
| trasparenza, trasformazioni | 0,15% | 0,17% |

Entrambe al livello delle differenze di antialiasing fra motori. WASM dà pixel identici al nativo.

### Velocità (ms per pagina)

| Pagina | Comandi | Skia nativo | Blend2D nativo | **Skia WASM** | **Blend2D WASM** |
|---|---|---|---|---|---|
| documenti comuni (4 pagine) | 3–196 | 3–14 | 1–6 | 50–65 | **8–14** |
| mappa catastale | 3.247 | 107 | 84 | 448 | **100** |
| disegno tecnico, 2.633 clip | 100.744 | 676 | 333 | 1.391 | **489** |
| pagina vettoriale densa | 193.303 | 76 | 111 | 172 | **150–198** |

### Peso del modulo WASM (motore + banco di prova)

| | .wasm | gzip | in più rispetto al solo motore |
|---|---|---|---|
| solo motore | 442 KB | 191 KB | — |
| + Blend2D | 1.556 KB | 520 KB | **+329 KB** |
| + Skia | 2.128 KB | 829 KB | +638 KB |

## Cosa manca a ciascuno (lavoro nostro)

**Blend2D**
- **Clip su tracciati**: solo rettangoli nativi. Nel prototipo: rettangolo allineato → clip nativo (il
  caso comune), altrimenti livello + maschera A8 composto al `Q`. Funziona ed è misurato sopra.
- **Trattini**: le opzioni esistono ma lo stroker **non le applica** (verificato nel sorgente): un
  "dasher" nostro, ~200 righe (lunghezza d'arco, curve appiattite).
- **4 modi di fusione non separabili** (Hue, Saturation, Color, Luminosity): per pixel sui livelli.
  I 12 separabili ci sono.
- **Gruppi di trasparenza e soft mask**: sul nostro sistema di livelli (con Skia sarebbe `saveLayer`).
- Rischio: **un solo manutentore principale**, API pre-1.0 che cambia (rinominata in snake_case da
  poco). Mitigazione: commit fissato con SHA, aggiornamenti voluti, e l'interfaccia `RasterBackend`
  che tiene Skia sostituibile.
- Nota: il runtime va inizializzato esplicitamente (`BLRuntimeInitializer`) — senza, in WASM il linker
  lo toglie e la prima allocazione va in crash (trovato e risolto nel prototipo).

**Skia**
- Funzionalità complete: clip, trattini, 16 modi di fusione, `saveLayer`, gradienti PDF.
- **In WASM è 3–5 volte più lento** di Blend2D sulle pagine comuni e pesa il doppio.
- **Build**: GN + depot_tools, gigabyte di sorgenti; non gestibile sul VPS/Jenkins né sulla macchina
  locale con la sua memoria. Resterebbe la dipendenza da **binari precompilati di terzi** (JetBrains),
  che non possiamo riprodurre né verificare — in contrasto con il principio 7 (dipendenze fissate e
  verificate) e con la sicurezza della catena di fornitura.

## Raccomandazione

**Blend2D**, integrato dai sorgenti con FetchContent (SHA fissato) come le altre dipendenze, per:
velocità in WASM (3–5×), peso (metà), build semplice e riproducibile, licenza zlib. Il lavoro in più
(clip con maschere — già prototipato —, trattini, 4 modi di fusione, gruppi) è codice nostro ben
delimitato, testabile contro i riferimenti. L'interfaccia `RasterBackend` resta, così un eventuale
passaggio a Skia non tocca l'interprete.

## Decisione

**Blend2D** (2026-09-28), dai sorgenti con FetchContent e SHA fissato, con le integrazioni elencate
sopra a nostro carico: clip su tracciati con maschere (e scorciatoia per i rettangoli), trattini, i 4
modi di fusione non separabili, gruppi e soft mask sul nostro sistema di livelli. JIT (AsmJit) nelle
build native di rilascio; senza JIT in WASM e nelle build con sanitizer e fuzzing, dove le pipeline di
riferimento in C++ sono controllabili. L'interfaccia verso il rasterizzatore resta separata
dall'interprete.
