# Piano di implementazione

Deriva da `CAPRIOLE.md` §9. Ogni fase chiude con qualcosa di **verificabile** (criterio di uscita)
e non si passa alla successiva con il gate rosso. Le stime sono ordini di grandezza per una persona con
assistenza AI e servono a vedere le proporzioni, non a promettere date.

Legenda: ✅ fatto · 🔜 prossima · ⬜ da fare · ❓ decisione aperta che blocca la fase

---

## Fase 1 — Scheletro del repository ✅

- [x] Monorepo, CMake + preset (`debug`, `release`, `asan`, `fuzz`, `wasm`), C++23 (ADR 0001).
- [x] `engine/core`: `Error`/`Result<T>`, `ByteSource` (memoria, file).
- [x] `parser/header`: riconoscimento `%PDF-x.y` con offset (primo codice reale, primo fuzzer).
- [x] `capi/` API C stabile, `bindings/wasm` ES module + smoke test Node.
- [x] `tools/cpdf` CLI di sviluppo.
- [x] GoogleTest, target libFuzzer `fuzz_header` con corpus seme.
- [x] Gate `scripts/check.sh` (clang-format, ASan/UBSan, test, clang-tidy) eseguito dall'hook `pre-push`;
      CI su Jenkins (`Jenkinsfile`, immagine `ci/Dockerfile`): gate + WASM, release Clang, fuzz smoke.
- [x] Toolchain installata (GCC 13, Clang 20, emsdk 6.0.10), gate completo verde in locale, primo push.
- [x] Fuzzing: `fuzz_header` 10 minuti, ~300 milioni di esecuzioni, nessun problema.
- [x] Job multibranch `capriole` su Jenkins (credenziale `capriole-github-pat`, webhook push), primo build verde su `development`.

**Uscita:** gate completo verde in locale (WASM compreso) e fuzz smoke pulito. ✅

## Fase 2 — Lexer, parser rigoroso, modello basso 🔜 (~4–6 settimane) — si lavora su `development`

Passi: **2a** lexer ✅ · **2b** oggetti + parser ✅ · **2c** 🔜 xref/trailer/object stream + FlateDecode ·
**2d** `Document` con stato delle modifiche · **2e** cifratura in lettura. Un branch `feat/…` per passo.

1. ✅ **Lexer** (ISO 32000-2 §7.2–7.3): whitespace/delimitatori, commenti, numeri (interi/reali, limiti),
   stringhe letterali (escape, bilanciamento parentesi, EOL), stringhe esadecimali, nomi (`#xx`),
   parole chiave. Lavora su `ByteSource` tramite `SourceReader` (finestra scorrevole); ogni token porta
   offset e lunghezza. Limiti in `ParseLimits`. Fuzzer `fuzz_lexer` con controllo differenziale sulla
   dimensione della finestra. `cpdf tokens`.
2. ✅ **Oggetti** (§7.3): `model::Object` (variant: null, bool, int64, real, string con forma
   letterale/esadecimale, name, array, `Dict` ordinato, `Stream` con posizione dei dati non caricati,
   `ObjectRef`). `parser::ObjectParser`: oggetti diretti, `n g R` con lookahead a errori differiti,
   oggetti indiretti `n g obj … endobj`, stream con EOL rigoroso e `/Length` diretto o tramite
   `LengthResolver`. Chiavi duplicate = errore (la scelta spetta alla riparazione). Limite di
   annidamento, verificato anche contro lo stack WASM (1 MB, test con `STACK_OVERFLOW_CHECK=2`).
   Fuzzer `fuzz_object`. `cpdf object <file> <offset>`. Interning dei nomi / arena: rimandati a quando
   le misure sul corpus li giustificheranno.
3. **Struttura del file** (§7.5): `startxref`, xref table classica, trailer, catena `/Prev`,
   **xref stream** (§7.5.8), **object stream** (§7.5.7), file ibridi (`/XRefStm`), linearizzazione
   (solo lettura: si ignora l'hint table ma non si deve rompere).
4. **Filtri minimi**: FlateDecode (+ predictor PNG/TIFF), necessario per xref/object stream. → zlib.
5. **Modello basso** (`model/`): `Document` con tabella degli oggetti `(num, gen) → slot`, caricamento
   lazy, stato per oggetto (`Unmodified/Modified/New/Deleted`), revisione di provenienza (quale
   sezione incrementale lo ha definito — serve a firme e salvataggi incrementali).
6. **Cifratura in lettura** (§7.6): Standard security handler R2–R6, RC4/AES-128/AES-256,
   `/Identity` crypt filter; implementazione propria di MD5, RC4, AES, SHA-256/384/512 (piccole,
   testate con vettori NIST). Password utente/proprietario.
7. `cpdf`: `info`, `xref`, `object <n>`, `dump`.
8. Fuzzer: `fuzz_lexer`, `fuzz_object`, `fuzz_document` (apertura completa).

**Uscita:** apre correttamente (confronto con `qpdf --check` e `qpdf --json`) ≥ 99% di un campione di
1.000 file SafeDocs *non* malformati; zero crash/ASan su UNSAFE-DOCS campione; fuzzer 24 h senza crash.

## Fase 3 — Writer (~2 settimane)

- Serializzazione degli oggetti (forma canonica ma fedele: numeri reali senza perdita, stringhe con
  la forma originale quando non modificate).
- **Salvataggio completo** (riscrive tutto, eventualmente con object/xref stream e compressione).
- **Salvataggio incrementale**: appende solo gli oggetti `Modified/New/Deleted`, nuova sezione xref
  nella **stessa forma** dell'ultima (tabella o stream), `/Prev` corretto, ricifratura se cifrato.
- Test fondamentali (spec §9.3):
  - round-trip senza modifiche → documento equivalente (confronto oggetto per oggetto via `qpdf --json`);
  - incrementale → i primi N byte del file sono **identici al byte** all'originale;
  - file con firma: dopo un salvataggio incrementale la firma resta valida (verifica esterna: `pdfsig`).

**Uscita:** i tre test sopra verdi sul campione della fase 2.

## Fase 4 — Harness di test sul corpus (~2 settimane, in parallelo alla 3)

- `corpus-tools/` (Python): download di un sottoinsieme deterministico di SafeDocs (per shard/indice,
  non tutti gli 8 TB), UNSAFE-DOCS, raccolta dei file dai bug tracker (pdf.js, PDFium, Poppler, MuPDF,
  qpdf) con fonte e ID del bug.
- Runner differenziale: per ogni file esegue `cpdf` e i riferimenti (qpdf, mutool, pdfium, pdf.js via
  Node) **in sandbox** (processo separato, timeout, limite di memoria), normalizza gli output (numero
  pagine, oggetti, errori) e produce un report con la **regola di maggioranza** della spec §7.3.
- Report storicizzati: ogni esecuzione confronta con la precedente (regressioni in rosso).
- Fuzzing continuo in locale (corpus di lavoro fuori dal repo); obiettivo OSS-Fuzz → richiede licenza.

**Uscita:** un comando produce il report di un campione di 10.000 file in < 1 ora.

## Fase 5 — Strato di riparazione (continuo, guidato dal report)

- Ricostruzione della xref scansionando `N G obj`; `/Length` errati (ricerca di `endstream`);
  stream corrotti con decompressione parziale; file troncati; offset relativi all'header spostato;
  catene `/Prev` cicliche; trailer mancanti (ricerca di `/Root`).
- Ogni riparazione è in `repair/`, attivata solo se il parser rigoroso fallisce, e annota cosa ha
  riparato (diagnostica mostrabile e utile al §8 della spec).

**Uscita:** sul campione, il numero di pagine coincide con la maggioranza dei riferimenti ≥ 99,5%.

## Fase 6 — Interprete dei content stream e renderer (~4–6 mesi, il grosso)

- ❓ **Decisione Skia vs Blend2D** all'inizio della fase: prototipo di 1–2 settimane con entrambi dietro
  un'interfaccia `render/RasterBackend`, benchmark su soft mask, gruppi di trasparenza isolati/knockout,
  blend mode, dimensione del binario WASM, facilità di build con Emscripten.
- Interprete (§8–9): stato grafico, CTM, clipping, tracciati, testo, XObject form/immagine, pattern,
  shading (tutti e 7 i tipi), trasparenza (§11), spazi colore inclusi ICC/Separation/DeviceN/Indexed.
- Font (§9.6–9.10): Type1, TrueType, CFF, Type3, CID, encoding, CMap predefinite (risorse Adobe,
  licenza BSD), ToUnicode; font standard 14 sostituiti con equivalenti liberi (URW base35 è AGPL →
  usare alternative con licenza compatibile, es. Liberation/TeX Gyre: **da verificare**).
- Filtri immagine: DCT, JPX, CCITT (proprio), JBIG2 (proprio), LZW, RunLength, ASCII*.
- Test differenziale sui pixel contro mutool/pdftoppm/pdfium con tolleranza, report per categoria.

**Uscita:** su un campione di 1.000 pagine, differenza di pixel sotto soglia rispetto alla maggioranza
dei riferimenti ≥ 95% (il "95% raggiungibile" della spec).

## Fase 7 — Estrazione del testo con provenienza (~1–2 mesi)

Glifi → parole → righe → blocchi/colonne; ogni elemento conserva la provenienza (content stream,
indice dell'operatore, oggetto). Base del modello alto, della ricerca, della redaction e della
selezione. Uscita: confronto con `pdftotext`/`mutool draw -F txt` su campione.

## Fase 8 — Prime funzioni utente e app

In ordine di valore/rischio: operazioni sulle pagine (unisci, dividi, riordina, ruota) → annotazioni
→ moduli AcroForm → allegati/Factur-X → redaction reale → firma PAdES → PDF/A → **editing del testo**.
In parallelo nasce la **UI web** (❓ framework: proposta Svelte 5, coerente con gli altri progetti del
workspace) che usa il modulo WASM in un Worker; poi la shell desktop (❓ Tauri).

---

## Decisioni aperte (con la fase in cui diventano bloccanti)

| Decisione | Blocca | Nota |
|---|---|---|
| Licenza di Capriole | pubblicazione, OSS-Fuzz, contributi | proposta da discutere: Apache-2.0 o MPL-2.0 |
| Skia vs Blend2D | fase 6 | prototipo + benchmark |
| JBIG2 / CCITT con licenza compatibile | fase 6 | implementazione propria |
| Libreria crittografica per PAdES | fase 8 | mbedTLS vs OpenSSL 3 (entrambe Apache-2.0) |
| Firma qualificata nel browser | fase 8 | smart card non accessibili dal web → desktop o firma remota |
| Framework UI / shell desktop | fase 8 | Svelte 5 + Tauri proposto |
| Package manager (vcpkg?) | fase 6 | quando le dipendenze native diventano 5+ |
| Marchio/dominio/GitHub org | pubblicazione | non tecnico |
