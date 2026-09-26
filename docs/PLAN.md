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

Passi: **2a** lexer ✅ · **2b** oggetti + parser ✅ · **2c** xref/trailer/object stream + FlateDecode ✅ ·
**2d** ✅ `Document` con stato delle modifiche · **2e** cifratura in lettura ✅. Un branch `feat/…` per passo.

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
3. ✅ **Struttura del file** (§7.5): `find_startxref` (ultimi 1024 byte, `%%EOF` obbligatorio),
   `read_xref`: tabella classica (voci rigorose da 20 byte), xref stream (`/W`, `/Index`, tipi 0/1/2),
   file ibridi (`/XRefStm`), catena `/Prev` con rilevamento dei cicli e limite di sezioni; la lista
   delle sezioni (offset, forma) è conservata per il salvataggio incrementale. `ObjectStream` (§7.5.7)
   e `ObjectLoader`: carica per numero dall'offset o dall'object stream, `/Length` e `/Filter`
   indiretti risolti, riferimenti a oggetti liberi/assenti/generazione errata = null (§7.3.10), cicli
   fermati da `max_load_depth`. Linearizzazione: letta come file normale.
4. ✅ **Filtri minimi**: FlateDecode con zlib 1.3.1 (limite anti zip-bomb `max_decoded_stream_bytes`),
   predittori PNG (tutti i tipi) e TIFF 2 a 8/16 bit, catena `/Filter` + `/DecodeParms`. Gli altri
   filtri sono `kUnsupported` con il nome del filtro. `cpdf xref|object|stream|check`.
   Fuzzer `fuzz_xref` (con controllo di determinismo del caricamento) e `fuzz_flate`.

   **Prima misura su PDF reali** (238 articoli e guide pubbliche, 2026-09-26): `cpdf check` passa su
   225; le varianti con xref stream e object stream generate da qpdf su 226. I casi non superati sono
   6 file cifrati (fase 2e) e 7 malformazioni reali, tutte tollerate da qpdf con un avviso —
   **input della fase 5**: chiavi duplicate (`/Type` due volte, 3 file), oggetto 0 elencato due
   volte in una sezione (2), oggetto 0 marcato in uso (1), generazione 65536 sulla testa della lista
   libera (1).
   Filtri non ancora supportati incontrati (stream): DCT 170, CCITTFax 64, ASCII85 19, LZW 4, JBIG2 3,
   JPX 2, ASCIIHex 2. ASCII85, ASCIIHex, LZW e RunLength sono piccoli e vanno anticipati (con la 2d);
   DCT/JPX/CCITT/JBIG2 servono solo al rendering (fase 6).
5. ✅ **Modello basso** (`document/`): `Document::open` possiede la sorgente; oggetti caricati lazy e in
   cache, stato per oggetto (`Unmodified/Modified/New/Deleted`), `set/add/remove`, trailer del
   documento (senza le chiavi della xref stream) con modifiche tracciate, `changed_objects()` = ciò
   che il salvataggio incrementale scriverà; numeri mai riutilizzati, `/Size` ostile gestito.
   Stream con dati in memoria (`Stream::memory_data`) per gli stream nuovi/riscritti. Albero delle
   pagine (§7.7.3) con rilevamento dei cicli. Filtri anticipati: ASCIIHex, ASCII85, LZW (con
   `EarlyChange` e predittori), RunLength. `cpdf pages`, `cpdf check` con conteggio pagine.
   **Rimandato alla fase 3:** la "revisione di provenienza" per oggetto — le sezioni xref sono già
   conservate, l'informazione si ricava lì quando il writer e le firme ne avranno bisogno.

   **Misura (238 PDF pubblici):** `cpdf check` passa su 231 (i 7 restanti sono le malformazioni della
   fase 5); numero di pagine uguale a qpdf su 229, **nessun file con un conteggio diverso**; i 9 senza
   conteggio sono 6 cifrati (2e) e 3 con xref malformata. Filtri ancora da fare: solo immagini
   (DCT 162 stream, CCITT 57, JBIG2 3, JPX 2).
6. ✅ **Cifratura in lettura** (§7.6): `crypto/` con MD5, SHA-256/384/512, RC4, AES-128/256 CBC
   scritti in casa e verificati con i vettori RFC 1321, FIPS 180-4, FIPS 197, SP 800-38A, RFC 6229
   (più valori ai confini dei blocchi calcolati con Python). `StandardSecurityHandler`: /V 1, 2, 4, 5,
   /R 2–6 (incluso l'algoritmo 2.B iterato di R6), crypt filter /V2, /AESV2, /AESV3, /Identity,
   password utente e proprietario, `/EncryptMetadata`, filtro `/Crypt` per stream. Decifratura nel
   loader: stringhe degli oggetti nel file (non negli object stream, non nel dizionario `/Encrypt`,
   non il `/Contents` delle firme), dati degli stream (mai gli xref stream). `Document::open` con
   `OpenOptions.password`; `ErrorCode::kPasswordRequired`. Fixture: 10 varianti generate con qpdf
   (`tests/data/encrypted/make_fixtures.sh`). Fuzzer `fuzz_security`. `cpdf` legge la password da
   `CAPRIOLE_PASSWORD`.
   **Primo bug reale trovato dal fuzzing** (`fuzz_security` e `fuzz_xref`, entrambi in pochi minuti):
   `/V 5` con `/R 2–4` chiedeva alla derivazione MD5 una chiave da 32 byte, leggendo oltre un digest
   da 16 (stack-buffer-overflow). Corretto validando la coppia /V–/R (Tabella 21) più un controllo
   difensivo; gli input sono in `tests/regression/security/`, riprodotti da `regression_test.cpp`
   (verificato: senza la correzione ASan interrompe la suite).
   **Limiti noti:** password non normalizzate (niente SASLprep per R6, niente conversione UTF-8 →
   PDFDocEncoding per R2–4); `/Perms` di R6 non verificato; AES non a tempo costante (va bene per
   aprire documenti, non per un servizio che decifra su richiesta).
7. ✅ `cpdf`: `header`, `tokens`, `xref`, `object`, `object-at`, `stream`, `pages`, `check`.
8. ✅ Fuzzer: `fuzz_header`, `fuzz_lexer`, `fuzz_object`, `fuzz_xref` (con `Document`), `fuzz_filters`,
   `fuzz_security`.

   **Misura finale della fase 2 (238 PDF pubblici, 2026-09-26):** numero di pagine uguale a qpdf su
   235, **mai diverso**; i 3 restanti hanno la xref malformata (fase 5). `cpdf check` pulito su 231
   (i 7 restanti: malformazioni della fase 5). Stream non decodificati: solo immagini (DCT, CCITT,
   JBIG2, JPX → fase 6).

**Uscita:** apre correttamente (confronto con `qpdf --check` e `qpdf --json`) ≥ 99% di un campione di
1.000 file SafeDocs *non* malformati; zero crash/ASan su UNSAFE-DOCS campione; fuzzer 24 h senza crash.

**Stato (2026-09-26):** tutte le funzionalità della fase sono fatte; il criterio d'uscita **non è ancora
verificato**: servono il campione SafeDocs/UNSAFE-DOCS (harness della fase 4) e un fuzzing di 24 h.
Finora: 238 PDF reali e ~10 minuti per fuzzer ad ogni passo.

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
