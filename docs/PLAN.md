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

**Stato (2026-09-26):** tutte le funzionalità della fase sono fatte. Criterio d'uscita:
- campione SafeDocs (1.000 file): **99,38%** dei file sani aperti ✅ (98,91% con la sola lettura
  rigorosa; +3 file con la prima riparazione, gli offset relativi all'header); nessun numero di pagine
  diverso dai riferimenti; i 4 restanti sono violazioni della specifica verificate a mano;
- UNSAFE-DOCS: **0 crash, 0 timeout** su 2.490 file (x002: 100% dei file sani aperti; x008: 98,17%) ✅;
- fuzzing di 24 h: in locale, a bassa intensità, con `scripts/fuzz_local.sh start 24` (1 core, 1,5 GB,
  nice 19, I/O idle; unità systemd utente `capriole-fuzz`). **Conclusa il 2026-09-28: 7 target,
  ~28 h, 0 problemi** (filters 378 M esecuzioni, header 6,1 G, lexer 286 M, object 380 M, security
  4 M — lento per costruzione, derivazione delle chiavi —, serialize 63 M, xref 14 M). Dettaglio:
  prima esecuzione (dal 2026-09-26 21:10)
  interrotta dopo ~13,8 h da un esaurimento di memoria **dell'intera macchina** (non del fuzzer, 423 MB
  sotto il suo tetto): `filters`, `header`, `lexer` completati, 4 h ciascuno, **0 problemi**; archivio
  in `CORPORA/reports/fuzz-campaign-20260926-partial/`. Ripresa il 2026-09-27 11:22 sui target mancanti
  (`object`, `security`, `xref`, più il nuovo `serialize`, 4 h ciascuno) con
  `fuzz_local.sh start 16 object security xref serialize`; archivio in
  `CORPORA/reports/fuzz-campaign-20260927-resumed/`. A seguire, 2026-09-28, `content` e `render` (i
  fuzzer della fase 6, allo stato di 6c): `content` 3 h, ~35,2 milioni di esecuzioni, **0 problemi**;
  `render` fermato dopo ~2 h e 6,9 milioni da un "out-of-memory" **falso**: heap vivo 46 MB, il resto era
  la quarantena di ASan (254 MB) piena di stato di libFuzzer; l'input, da solo, gira in 92 MB con il
  binario di allora e con quello attuale. Correzione: le campagne usano `quarantine_size_mb=64`
  (`scripts/fuzz_campaign.sh`). Archivio in `CORPORA/reports/fuzz-campaign-20260928-content-render/`.
  Poi 6 h su `function` e `render` (codice di 6f): `function` ~454 milioni di esecuzioni, **0 problemi**;
  `render` **un difetto vero** dopo 6.600 esecuzioni: con CTM astronomiche (form che si ridisegnano
  scalate fino a 10¹⁶ e oltre, tratti tratteggiati) lo stroker di Blend2D leggeva fuori dal bitmap
  (heap-buffer-overflow in lettura). Corretto nel nostro confine: la geometria oltre ±10⁶ pixel non
  arriva mai a Blend2D, viene portata in coordinate di dispositivo e ritagliata in doppia precisione
  sulla pagina (Sutherland–Hodgman per i riempimenti e i clip, Liang–Barsky per i tratti; testo e
  immagini a quelle scale non si disegnano). Caso minimizzato (596 byte) in
  `tests/regression/render/astronomic_ctm.pdf`, che la suite di regressione ora ridisegna come
  `fuzz_render`; verificato che senza la correzione il test fallisce. Archivio in
  `CORPORA/reports/fuzz-campaign-20260928-render-function/`.

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

**Uscita:** i tre test sopra verdi sul campione della fase 2. ✅ con una variante sul primo: il
round-trip è verificato con `qpdf --check`, rilettura rigorosa di Capriole e pagine uguali sui corpora,
e oggetto per oggetto nei test unitari — non con `qpdf --json` (resta da aggiungere all'harness se
servirà). La ricifratura dei file cifrati resta per 3d.

Passi: **3a** serializzatore ✅ · **3b** salvataggio completo ✅ · **3c** salvataggio incrementale ✅ ·
**3d** ricifratura ✅.

**3a (2026-09-26):** `writer::serialize` con contratto di round-trip (parse(serialize(x)) == x: tipi,
byte, forma delle stringhe, ordine delle chiavi), reali senza esponente con il minimo di cifre che
rilegge lo stesso double; `ByteSink` (`MemorySink`, `FileSink`); fuzzer `fuzz_serialize`.

**3b (2026-09-27):** `writer::save_full` + `cpdf save`. Riscrive tutto con una tabella xref classica:
numeri e generazioni conservati, oggetti degli object stream scritti come oggetti normali, object/xref
stream e dizionario di linearizzazione non scritti, dati degli stream copiati senza ricomprimerli e
`/Length` reale (corregge le lunghezze sbagliate lette dalla riparazione), oggetti illeggibili scritti
come null (i riferimenti restano validi), eliminati con generazione +1. Deterministico: `/ID` = [primo
originale, MD5 dei byte scritti]; `/ModDate` solo se il chiamante la passa. **Le riparazioni sono solo
in memoria, quindi il writer scrive esplicitamente ciò che hanno trovato:** se l'albero delle pagine
letto rigorosamente non dà le pagine trovate (o il `/Count` della radice non torna, o la radice ha
`/Parent`) l'albero si riscrive piatto — radice con tutte le pagine, `/Type` e `/Parent` corretti,
attributi ereditati (`Resources`, `MediaBox`, `CropBox`, `Rotate`) copiati dai nodi lasciati fuori,
pagina illeggibile → pagina bianca, `/MediaBox` di default (Letter) se persa con i nodi danneggiati,
radice che era essa stessa l'unica pagina → nuova radice. Il `Document` del chiamante non cambia.

Round-trip dell'harness (`run_corpus.py --roundtrip`: salva, `qpdf --check` sull'uscita, rilettura
**rigorosa** con Capriole, pagine uguali):

| Corpus | Salvati | qpdf senza errori | Rilettura rigorosa | Pagine conservate |
|---|---|---|---|---|
| SafeDocs 0000 | 977/996 (19 cifrati → 3d) | **977/977** (0 avvisi) | 973/977 | **977/977** |
| UNSAFE x002 | 1.410/1.416 | 1.408/1.410 | 1.360/1.410 | **1.410/1.410** |
| UNSAFE x008 | 941/941 | 868/941 | 835/941 | **941/941** |

Ogni rilettura rigorosa fallita e ogni errore qpdf residuo verificati per categoria: sono **dati degli
stream già corrotti nel file d'origine**, copiati fedelmente (Flate troncato o con header sbagliato,
LZW con codici non definiti, RunLength, righe del predittore, filtri con nome storpiato, JPEG rotti).
Il primo giro aveva pagine conservate solo 485/941 su x008: l'albero riparato non veniva scritto.
Possibile più avanti (non deciso): opzione per ricodificare gli stream danneggiati al salvataggio.

**3c (2026-09-27):** `writer::save_incremental` + `cpdf save --incremental [--moddate D:…]`. Copia il
file originale a blocchi (mai intero in memoria), poi — solo se qualcosa è cambiato — appende gli
oggetti `Modified`/`New`, le voci libere dei `Deleted` (generazione +1, catena da 0) e una nuova
sezione xref **nella stessa forma dell'ultima** (tabella; xref stream senza filtri per i file a stream;
tabella per gli ibridi), con `/Prev` alla sezione di `startxref` e `/ID` [primo originale, MD5 dei byte
fino alla sezione]. Senza modifiche l'uscita è il file originale. Rifiutati con `kUnsupported`: i file
cifrati (3d) e quelli che hanno richiesto riparazioni — l'albero delle pagine viene letto prima di
decidere, così la scelta non dipende da cosa ha fatto il chiamante; per loro c'è il salvataggio
completo. Parti comuni ai due salvataggi in `src/writer/save_support.*`.

Firme: i 7 PDF firmati di SafeDocs (6 a tabella, 1 linearizzato a xref stream), salvati in modo
incrementale con `/ModDate` cambiata → **firme ancora valide per `pdfsig`** ("not total document
signed", come atteso: l'aggiornamento è fuori dal `/ByteRange`), `qpdf --check` pulito, prefisso
identico al byte.

Round-trip incrementale dell'harness (`--roundtrip` ora fa entrambi i salvataggi; l'incrementale
cambia `/ModDate` perché ci sia qualcosa da appendere):

| Corpus | Salvati (gli altri: riparazioni o cifrati) | Prefisso identico | qpdf non peggiore dell'originale | Rilettura rigorosa dove l'originale la regge | Pagine conservate |
|---|---|---|---|---|---|
| SafeDocs 0000 | 958/996 (19 riparazioni, 19 cifrati) | **958/958** | **958/958** | **927/927** | **958/958** |
| UNSAFE x002 | 1.285/1.416 (125 riparazioni, 6 cifrati) | **1.285/1.285** | **1.285/1.285** | **1.225/1.225** | **1.285/1.285** |
| UNSAFE x008 | 372/941 (569 riparazioni) | **372/372** | **372/372** | **227/227** | **372/372** |

Gli altri file salvati che non si rileggono in modo rigoroso non si rileggevano nemmeno prima: i loro
difetti sono in oggetti che la modifica non tocca, e l'incrementale li lascia com'erano (come deve).
0 crash, 0 regressioni di lettura.

**3d (2026-09-27):** i documenti cifrati si salvano cifrati, in entrambi i modi, con il loro
`/Encrypt`, la loro chiave e i loro metodi (RC4, AES-128, AES-256; R2–R6, crypt filter): il documento
tiene tutto decifrato, e il writer cifra di nuovo stringhe e dati degli stream di ogni oggetto per il
numero con cui lo scrive — non il dizionario `/Encrypt`, le xref stream, il `/Contents` delle firme,
i metadata con `/EncryptMetadata false`. La regola che sceglie il metodo di uno stream
(`method_for_stream`) è una sola, condivisa da lettura e scrittura. Il primo `/ID`, che entra nella
chiave, resta quello del file (vuoto se mancava). **IV di AES derivato, non casuale**: SHA-256 di
chiave, oggetto e dati — per chi non ha la chiave è imprevedibile quanto uno casuale, e l'uscita resta
deterministica (principio 6); rivela solo che dati uguali nello stesso oggetto cifrano uguali.
Non ancora: cambiare o togliere la protezione (opzione di salvataggio futura); i permessi `/P` li fa
rispettare l'applicazione, non il writer.
Verifiche: per ognuna delle 10 fixture cifrate (R2–R6, object stream, metadata in chiaro, password
utente vuota), salvataggio completo e incrementale → riaperti in modo rigoroso con password utente e
proprietario (contenuto, stringhe, metadata uguali), password sbagliata rifiutata, testo in chiaro
assente dal file; **qpdf --check pulito, Poppler e MuPDF estraggono il testo con la password** su
20/20 uscite, revisione invariata.

Corpora (i file cifrati reali: 19 in SafeDocs, 6 in x002, con password utente vuota): SafeDocs ora
salvato **996/996** in modo completo (prima 977: i 19 cifrati erano rifiutati), qpdf pulito 996/996,
pagine conservate 100%; incrementale 977/996 (i 19 rifiutati ora sono solo i file riparati), byte
originali intatti e qpdf non peggiore 100%. x002: completo **1.416/1.416**, pagine conservate 100%;
incrementale 1.290 (126 rifiutati, tutti per riparazioni). 0 crash, 0 regressioni.

## Inspector — app demo nel browser ✅ (2026-09-27, anticipo della fase 8)

`apps/inspector`: il motore WebAssembly in un Web Worker, attraverso l'API C estesa (documento opaco,
riepilogo e oggetti in JSON, dati degli stream, salvataggio; formato in `capi/README.md`), con una UI
Svelte 5 statica: panoramica (versione, cifratura con permessi, sezioni xref, trailer, **riparazioni**),
pagine, oggetti con riferimenti navigabili, dati degli stream decodificati o grezzi (testo/esadecimale,
download), apertura con password, lettura rigorosa su richiesta, salvataggio completo e incrementale,
tema chiaro/scuro/sistema. Nessun byte lascia il browser. Nel gate con `SKIP_WASM=0` (format,
type-check, build) e `SKIP_E2E=0` (5 test Playwright in Chromium: navigazione fino al content stream,
salvataggi con prefisso identico, password sbagliata e giusta, riparazioni elencate, tema). Diventerà il
pannello di debug dell'app vera (stessa UI in Tauri per il desktop).

## Fase 4 — Harness di test sul corpus (~2 settimane, in parallelo alla 3) — 🟡 prima versione attiva

**Fatto (2026-09-26):** `corpus-tools/fetch_corpus.sh` (SafeDocs 0000, UNSAFE x002 e x008 in
`../CORPORA/`, fuori dal repo) e `corpus-tools/run_corpus.py`: `cpdf check` sotto ASan + qpdf, MuPDF
(`mutool info`), Poppler (`pdfinfo`) come programmi esterni, maggioranza sul numero di pagine, report
`summary.md` + `results.jsonl` per esecuzione, confronto con l'esecuzione precedente. Isolamento:
processo separato, cartella vuota, timeout, `prlimit` 4 GB; **niente namespace di rete** (non
disponibile senza privilegi). Mancano: PDFium e pdf.js come riferimenti, file dai bug tracker, scala
10.000 file.

**Risultati della prima esecuzione:**

| Corpus | File | Crash (ASan/UBSan) | Timeout | Pagine = maggioranza | Pagine diverse | File "sani"* aperti |
|---|---|---|---|---|---|---|
| SafeDocs 0000 | 1.000 | **0** | 0 | 977 / 996 | **0** | 637 / 644 (98,91%) |
| UNSAFE x002 (evalFour) | 1.417 | **0** | 0 | 1.293 / 1.413 | **0** | 995 / 1.027 (96,88%) |

\* "sani" = `qpdf --check` senza avvisi e i tre riferimenti concordi. **Tutti** i file sani non aperti
sono stati verificati a mano: sono violazioni della specifica che qpdf ripara in silenzio, nessun bug
di Capriole. Il caso dominante sono i **byte spuri prima di `%PDF-`** (3 su 7 in SafeDocs, 32 su 32 in
x002), che spostano tutti gli offset. Con quella sola riparazione SafeDocs passerebbe al 99,38%.

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

**Architettura (2026-09-26):** `Document::open` tenta la lettura rigorosa; solo se fallisce, e solo per
errori strutturali (`repair::may_help`: non password, non funzioni non supportate, non limiti), prova le
riparazioni in ordine fisso e registra quelle applicate in `Document::repairs()` (il writer ne terrà
conto). `OpenOptions.repair = false` / `CAPRIOLE_STRICT=1` per la sola lettura rigorosa. L'harness conta
i file aperti grazie a una riparazione e le riparazioni per tipo.

**Riparazioni implementate (2026-09-26)**, tutte registrate in `Document::repairs()` e provate
dall'harness (0 regressioni tra un'esecuzione e l'altra):
1. ✅ `header_offset` — byte spuri prima di `%PDF-` e offset relativi all'header: lettura attraverso
   una vista che parte dall'header (`OffsetByteSource`).
2. ✅ `xref_reconstructed` — xref mancante, troncata o malformata (niente `startxref`, voci da 19 byte,
   generazioni fuori limite, oggetto 0 doppio): scansione del file (`repair/reconstruct`) per `n g obj`,
   `trailer`, xref stream (chiavi del trailer) e object stream; per ogni oggetto vince la definizione
   più avanti nel file, diretta o compressa (quella compressa datata dal suo object stream).
3. ✅ `tolerant_parsing` — `ParseMode::kTolerant` del parser, usato solo dallo strato di riparazione:
   chiavi duplicate (vince l'ultima), `stream` seguito da spazi / CR da solo, `/Length` mancante o
   sbagliata (fino a `endstream`, o a fine file se il file è troncato), `endobj` mancante o storpiato.
4. ✅ `object_relocated` / `missing_object` — voce xref che punta nel posto sbagliato: l'oggetto si prende
   dove lo trova la scansione; se non esiste nel file (es. voce "in uso" con offset 0) vale null.
5. ✅ `page_tree_type` — nodo dell'albero delle pagine senza `/Type` o con un `/Type` sbagliato
   (compreso `/Page` con `/Kids`, come fa qpdf): `/Pages` se ha `/Kids`, `/Page` altrimenti. Una radice
   senza `/Kids` con un `/Type` estraneo (es. il catalogo punta a un font) non è un albero di pagine;
   senza `/Type` vale come l'unica pagina, come per i riferimenti.
6. ✅ `partial_decode` — dati Flate/LZW troncati o corrotti, predittore con l'ultima riga incompleta,
   filtri ASCII/RunLength senza marcatore di fine: si tiene la parte decodificata.

**Risultato** (con la sola lettura rigorosa → con le riparazioni):

| Corpus | File sani aperti | Pagine = maggioranza | Pagine diverse | Crash |
|---|---|---|---|---|
| SafeDocs 0000 | 98,91% → **100%** | 977/996 → **996/996** | 0 | 0 |
| UNSAFE x002 | 96,88% → **100%** | 1.293/1.414 → **1.408/1.414** | 0 | 0 |

L'harness ha anche trovato un errore di precedenza nella ricostruzione (definizione diretta contro
object stream più recente: 16 pagine invece di 21), corretto e coperto da test.

7. ✅ `page_tree_rebuilt` — albero delle pagine che non torna (visita fallita, o meno pagine del
   `/Count` della radice): si ricostruisce da `/Kids` più i collegamenti `/Parent` (recupera le pagine
   che il genitore ha perso dal suo `/Kids`). Scelta fra tre candidati — visita, ricostruzione,
   ricostruzione con i figli illeggibili tenuti come pagine segnaposto: vince quello che raggiunge
   esattamente il `/Count`; altrimenti la ricostruzione, se recupera pagine **senza superare** il
   `/Count` (più pagine del dichiarato = residui, es. pagine di revisioni vecchie); altrimenti la visita.

**UNSAFE x008** (1.073 file TA3, malformazioni sintetiche): **0 crash, 0 timeout**; pagine uguali alla
maggioranza 718 → 887/963 (poi **959/963**, vedi sotto), file sani 164/164. I 29 conteggi diversi di allora erano **tutti per difetto**
(nessuna pagina inventata): 4 file con `/Count` gonfiato (278 dichiarate, 22 pagine nel file: il nostro
conteggio è quello vero, i riferimenti riportano il `/Count`), 25 ricostruzioni parziali di alberi
distrutti ad arte (prima: errore). Su SafeDocs 996/996 e x002 1.410/1.414 invariati o migliorati, 0
regressioni in ogni esecuzione.

**Miglioramento x008 (2026-09-27)**, dopo l'annotazione "da migliorare" (887/963). Analisi caso per
caso: su questi file qpdf `--show-npages` e pdfinfo riportano il `/Count` della radice (la visita di qpdf
trova meno pagine, e conta come pagine anche stream e font); ogni file contiene inoltre un secondo
albero orfano con le stesse pagine. Il riferimento affidabile è quindi il numero di oggetti pagina
dell'albero vero, verificato a mano sui casi. Riparazioni aggiunte:
8. ✅ ricostruzione dell'albero: un figlio che non è più un nodo (illeggibile, o un altro oggetto con
   quel numero: stream, font) ma che le pagine nominano come `/Parent` passa loro il posto; un oggetto
   estraneo al posto di una pagina (`looks_like_page`: `/Type` diverso da Page senza `/MediaBox`) non è
   una pagina — e il writer lo lascia com'è, scrivendo una pagina bianca al suo posto se serve; `/Kids`
   scritto male (`/Kid`, altre maiuscole) vale come `/Kids`; un nodo `/Page` con `/Kids` è `/Pages`.
9. ✅ `tolerant_parsing` più robusto: array senza `[` (`/MediaBox 0 0 612 792 ]`, `/ProcSet /PDF /Text ]`)
   o senza `]` (l'array finisce dove ricominciano le chiavi, che tornano al dizionario), riferimenti
   senza generazione (`66 R`), stringhe senza `(` (valore null, parole e `)` saltate), token estranei
   saltati fino alla chiave successiva; intestazioni `n g obj` illeggibili o storpiate (numero oltre
   64 bit, `0 90 obj`, `87 80 obj`) prese come l'oggetto che la tabella indica in quel punto.
10. ✅ tabella xref inaffidabile: se la lettura rigorosa riesce ma `/Root` non porta a un dizionario, o
   se su un campione di 32 voci almeno un quarto non porta al proprio oggetto (tabella spostata di
   qualche oggetto), la tabella si ricostruisce per scansione — adottata solo se porta a un catalogo.
11. ✅ `catalog_found` — `/Root` che non è un dizionario: il catalogo è l'oggetto `/Type /Catalog` con
   `/Pages` (il numero più alto se più d'uno); `/Root` corretto in memoria, così il salvataggio completo
   lo scrive giusto.

**Risultato su x008:** pagine uguali alla maggioranza **887 → 959/963**, file senza conteggio 47 → **0**,
0 crash, 0 regressioni. I 4 file diversi hanno un `/Count` gonfiato (278, 157, 109, 2) che qpdf e pdfinfo
ripetono: il nostro conteggio è quello delle pagine realmente nell'albero (22, 22, 60, 1), verificato
contando gli oggetti pagina del file. Round-trip x008: salvati 1.039/1.039 (prima 941: più file ora
leggibili), pagine conservate 100% sia nel salvataggio completo sia nell'incrementale.
SafeDocs invariato (996/996, nessun falso allarme del controllo della tabella: l'incrementale salva
ancora 958 file). x002: 1.410/1.414 invariato; un file cambia da 4 a 9 pagine — troncato, il nodo 469
manca ma le sue 5 pagine esistono e lo nominano come `/Parent`: 9 è il `/Count` e il conteggio di qpdf,
4 quello di MuPDF e Poppler (la "maggioranza" dell'harness). Lo teniamo: è l'effetto voluto della
riparazione 8.

**Limiti noti:** object stream non ricostruibili nei file cifrati (i dati non si decifrano prima che il
gestore di sicurezza esista); dati attaccati alla parola `stream` (`streamDATA`) non riconosciuti; i 6 file
x002 restanti sono troncati in modo grave.

## Fase 6 — Interprete dei content stream e renderer (~4–6 mesi, il grosso) — ✅ completata (2026-09-30)

Passi: **6a** parser dei content stream ✅ · **6b** interprete (stato grafico, CTM, tracciati,
clipping, colori → lista di comandi di disegno indipendente dal rasterizzatore) ✅ · **6c** prototipo
Skia vs Blend2D dietro la stessa interfaccia, benchmark e dimensione WASM → **Blend2D** ✅
([ADR 0002](adr/0002-rasterizzatore.md)) · **6d** rendering con Blend2D nel motore, confronto
pixel con pdftoppm, anteprima raster nell'Inspector ✅ · **6e** trasparenza: modi di fusione, gruppi
di trasparenza, soft mask ✅ · **6f** funzioni PDF, knockout e gruppi non isolati esatti, `/TR`,
immagini senza codec ✅ · **6g** JPEG con libjpeg-turbo ✅ · **6h** gli altri codec (CCITT, JBIG2, JPX) ✅ ·
**6i** font ✅ · **6j** sfumature e pattern ✅ · **6k** annotazioni ✅ · **6l** gestione del colore ✅ · **6m** linee sottili ✅ · **6n** confronto su 1.000 pagine ✅ (criterio di uscita).

**6n (2026-09-30) — criterio di uscita della fase 6: raggiunto.** `corpus-tools/compare_render.py`
(versionato, documentato in `corpus-tools/README.md`): una pagina per file, scelta da un hash del seme e
del nome (lo stesso campione a ogni giro, non solo prime pagine), disegnata da Capriole (build release,
pacchetto di risorse), `pdftoppm` e `mutool draw` a 72 dpi dalla CropBox. Somiglianza: quota di pixel
che differiscono al massimo di 32/255 per canale dopo una sfocatura di 1 pixel; una pagina passa se è
≥ 0,95 rispetto ad almeno un riferimento (con due riferimenti la maggioranza è uno che concorda),
«stretta» se rispetto a entrambi. Rapporto in `CORPORA/reports/render-safedocs-0000-20260930-210831/`.

| Campione | Pagine | Passano | Strette | I riferimenti concordano fra loro |
|---|---|---|---|---|
| SafeDocs 0000 (+ 4 da x008) | 1.000 | **992 (99,2%)** | 912 (91,2%) | 894/999 (89,5%) |

Capriole è vicino a entrambi i riferimenti **più spesso di quanto essi concordino fra loro**; nessuna
pagina non disegnata, nessun timeout; 5 file esclusi perché nessun riferimento li disegna. Le 8 pagine
che non passano sono differenze di resa senza avvisi (tinte di una cornice, una griglia di tabella in
un'unica `f`, testo CJK). Il giro ha trovato e fatto correggere due difetti: **una `/SMask` (o `/Mask`)
più fine della sua immagine** — testo messo come immagini di un colore con la forma nella maschera
(0000079, 0000395, 0000504): la maschera veniva ridotta alla misura dell'immagine e il testo diventava
barre nere; ora l'immagine sale alla risoluzione della maschera — e **`/DecodeParms` con la stringa
`(null)`** al posto di null (0000337): la maschera non si leggeva più. Entrambi con test.

Restano, fuori dal criterio, per le fasi successive: il contenuto opzionale (`/OC`: livelli nascosti,
0000337 prima pagina), gli aspetti generati per campi modulo e FreeText senza `/AP`, i contorni riempiti
con un pattern (oggi in tinta unita).

**6m (2026-09-30):** il ritocco delle linee sottili, la *stroke adjustment* di §10.7.5 come la fanno
pdftoppm e Acrobat ("migliora linee sottili"). Prima un tratto sottile, portato a un pixel, cadeva a
cavallo di due pixel a metà intensità: tabelle e disegni tecnici uscivano sbiaditi. Ora, in spazio
dispositivo: i **tratti** su una pagina che conserva gli assi (penna fino a 4 pixel, o rotonda) hanno una
penna di pixel interi e i vertici dei segmenti orizzontali e verticali spostati perché il bordo vicino,
alla larghezza vera, cada su un confine di pixel; i **riempimenti fatti di un solo rettangolo** allineato
agli assi hanno i bordi arrotondati ai pixel, almeno un pixel di distanza (la regola di arrotondamento di
pdftoppm, verificata riga per riga su 0000527). Non si toccano glifi, diagonali, curve, né i riempimenti di
più rettangoli (la griglia di una tabella in un'unica `f`: pdftoppm la lascia com'è — 0000740). Due
errori trovati sul corpus e corretti prima del commit: rettangoli ricostruiti sempre nello stesso verso
(una cornice con il buco per la regola nonzero copriva tutta la pagina: 0000273, ora test di
regressione), e la larghezza minima usata al posto di quella vera per il bordo dei tratti. Test in
`render_test.cpp` (3). **Corpus:** 182 pagine migliorate, le peggiorate di al massimo 0,023;
**97,3% delle pagine sopra il 95%** (92,9% prima).

**6l (2026-09-30):** la gestione del colore ([ADR 0006](adr/0006-colore.md)), partita
dall'osservazione che i colori erano più accesi di quelli di Acrobat. **Little CMS 2.19.1** (MIT), un
contesto con budget di memoria per trasformazione, verso sRGB in colorimetrico relativo con
compensazione del punto nero: profili `ICCBased` (poi `/Alternate`), `CalRGB`/`CalGray` come profili
costruiti dai loro parametri, e per il **DeviceCMYK** l'intento di output del documento, altrimenti il
profilo di stampa **SWOP2006_Coated3v2** dal pacchetto di risorse, altrimenti una **tabella 9⁴**
campionata da quel profilo (20 KB nel motore). Un ciano pieno esce (0, 170, 235) come in Acrobat, non
più (0, 255, 255). Immagini a 8 bit con profilo convertite a righe intere. **Due correzioni trovate
strada facendo:** l'interprete fondeva due tinte piatte diverse alla stessa percentuale in un solo
colore (i cerchi blu di 0000861 uscivano nel colore dello sfondo); i nomi `/G`, `/RGB`, `/CMYK` valevano
come abbreviazioni anche quando erano nomi di risorse. Test in `icc_test.cpp` (5, profili costruiti con
Little CMS), fuzzer nuovo `fuzz_icc` (ha trovato la stessa conversione di puntatori a funzione di
OpenJPEG: `-fsanitize=function` spento solo per Little CMS). **Corpus:** 59 pagine migliorate,
nessuna peggiorata; **92,9% delle pagine sopra il 95%** (90,6% prima). Modulo WASM 3,57 MB, 1,43 MB gzip
(+103 KB: Little CMS e la tabella).

**6k (2026-09-30):** le annotazioni (§12.5), disegnate dopo il contenuto della pagina come fanno i
visualizzatori. L'aspetto normale (`/AP /N`, quello che `/AS` sceglie fra gli stati) viene collocato sul
`/Rect` con l'algoritmo di §12.5.5 (il `/BBox` passato per la `/Matrix` e mappato sul rettangolo), come
una form con la sua trasparenza, e con l'opacità `/CA` in un gruppo isolato. Non si disegnano le
annotazioni Hidden e NoView né i popup (sono finestre del visualizzatore). **Aspetti generati** per chi
non ne ha: Square, Circle, Line, Ink, Polygon/PolyLine (bordo `/BS` o `/Border` con tratteggio, colore
`/C`, interno `/IC`), Highlight (in Multiply), Underline, StrikeOut, Squiggly (`/QuadPoints`), e i bordi
dei link come li disegna pdftoppm (anche senza `/C`, in nero; `/U` sottolinea). Campi modulo, FreeText
e note senza aspetto: segnalati, non generati. Una pagina con il contenuto illeggibile mostra comunque
le sue annotazioni. Test in `annotation_test.cpp` (5), seme `annotations.pdf` per `fuzz_render`.
**Corpus:** sulle prime pagine 7 file migliorati (0000854: il testo dei campi modulo), nessuno
peggiorato; **90,6% delle pagine sopra il 95%**.

**6j (2026-09-29):** sfumature e pattern (§8.7). L'interprete prepara ogni sfumatura come
`graphics::Shading` (`content/shading`): colori già in sRGB attraverso funzione e spazio colore, una
rampa di 1024 campioni lungo t (assiali, radiali, mesh con funzione), una griglia 256×256 per quelle a
funzione (tipo 1); le mesh decodificate bit a bit (tipi 4 e 5, con `/Decode`), le patch di Coons e
tensoriali (6, 7, con i lati condivisi dei flag 1–3) suddivise in triangoli secondo la loro misura sulla
pagina. **I pixel si calcolano uno per uno dalla geometria** (`graphics/shading`): proiezione sull'asse,
il cerchio più grande che passa per il punto (radiali a due cerchi, anche a cono), `/Extend`,
`/BBox`, Gouraud sui triangoli (con un tetto di lavoro contro le mesh ostili); li usano il rasterizzatore
(sul riquadro del clip) e l'SVG (PNG a 1,5 pixel per punto). **Pattern:** lo spazio del pattern è quello
del flusso di contenuto che lo usa (pagina, form, glifo); riempimenti, testo e modi di clip con un
pattern diventano clip + pittura. Shading pattern con `/Background`; **tiling** come lista di disegno a
sé (clip al `/BBox`, cache per pagina; non colorati nel colore dato, i loro operatori di colore
ignorati), disegnata una volta in un'immagine alla risoluzione del dispositivo e ripetuta da Blend2D;
lungo un asse con un passo più lungo della pagina (sfondi con `/XStep 99999`) si disegna l'unica copia
visibile. Nell'SVG i tiling sono `<pattern>` vettoriali. I contorni con un pattern restano in tinta unita
(avviso). Test in `shading_test.cpp` (16), fuzzer nuovo `fuzz_shading`. **Corpus:** 44 pagine migliorate,
nessuna peggiorata; con la metrica sfocata il **90,4% delle pagine sopra il 95%** (88,4% prima); le
peggiori rimaste perdono per linee sottili (6l), contenuto opzionale e annotazioni (campi modulo: 0000854).
Modulo WASM 3,28 MB, 1,32 MB gzip (+21 KB).

**6i (2026-09-29):** i font ([ADR 0005](adr/0005-font.md)). **FreeType 2.14.3** per i soli contorni
(moduli sfnt/TrueType/Type 1/CFF/CID, niente hinting né interprete di bytecode, una libreria e un budget
di memoria per programma); tutto il resto nostro in `engine/src/font/`: encoding e `/Differences` (AGL
generata da `scripts/gen_glyph_tables.py`), scelta del glifo di §9.6.5 (TrueType simbolici per codice
nella cmap (3,0), non simbolici per nome), CMap (codespace 1–4 byte, cid/notdef, `usecmap`, bf per
ToUnicode), CIDToGIDMap, `/W` `/W2` e scrittura verticale, Type 3 eseguiti come contenuto (`d1` senza
colore), i modi di rendering con il clip a ET. **Non incorporati:** i 14 Foxit nel motore (metriche
Adobe: gli standard senza `/Widths` hanno le larghezze giuste), glifi adattati alle `/Widths`.
**Pacchetto di risorse** fuori dal motore (`scripts/fetch_resources.sh`, 34 MB: CMap Adobe, Noto Sans
CJK, Liberation): l'host lo registra, ogni pagina dice che cosa le è mancato; `cpdf render
--resources`, API C `capriole_resource_add`/`capriole_last_missing_resources_json`, l'Inspector lo
scarica su richiesta. L'anteprima SVG definisce ogni glifo una volta (`<use>`): 0,5–0,6 MB per una pagina
fitta invece di 3–4,5. Fixture di font scritte da zero (`tests/data/fonts/make_fixtures.py`: TrueType,
CFF, CFF CID-keyed, Type 1). Fuzzer `fuzz_font` (10 min, ~4,6 milioni di esecuzioni, pulito dopo
la correzione) e `fuzz_cmap` (5 min, pulito); `fuzz_font` ha trovato un `memcpy(p, NULL, 0)` dentro
FreeType: con i sanitizer `nonnull-attribute` è spento solo per FreeType, l'input è fra i semi.
**Corpus** (prima pagina dei 1.000 SafeDocs, 40 dpi, contro `pdftoppm -cropbox`, pixel entro tolleranza
dopo una leggera sfocatura): **88% delle pagine sopra il 95%** (con il pacchetto di risorse); le pagine
peggiori guardate a vista hanno il testo giusto e perdono per sfumature e pattern (6j), linee sottili,
e il contenuto opzionale (`/OC`, non ancora gestito: 0000337). Modulo WASM 3,23 MB, **1,30 MB gzip**
(+429 KB: i Foxit 232 KB, FreeType e tabelle il resto).

**6h (2026-09-29):** gli altri codec immagine. **CCITTFaxDecode** nostro (T.4 1D/2D e T.6, `/K`,
`/EncodedByteAlign`, `/BlackIs1`, EOL ed EOFB): identico a libtiff (via Pillow) su tutte le varianti
delle fixture (`tests/data/ccitt/make_fixtures.py`); le 46 pagine CCITT del corpus decodificano.
**JBIG2Decode** nostro (T.88; jbig2dec è AGPL, usato solo come oracolo esterno): coder aritmetico MQ,
IAx/IAID, tabelle Huffman B.1–B.15, regioni generiche, di raffinamento, di simboli, di testo, pattern
e halftone, `/JBIG2Globals`; budget di pixel per bitmap e totale, scalato col limite di uscita.
**378/378 stream del corpus identici pixel per pixel a jbig2dec.** Due divergenze volute da jbig2dec,
dove seguiamo la specifica (come pdfium e pdf.js): il contesto TPGRON col template 1 e l'offset del
riferimento alla pagina; documentate nello script delle fixture. **JPXDecode** con **OpenJPEG 2.5.4**
([ADR 0004](adr/0004-jpeg2000.md)): l'immagine descrive sé stessa (componenti, spazio colore, alfa;
sYCC → RGB; componenti sottocampionate riportate alla griglia piena), `/SMaskInData`, oltre 16 Mpixel
decodificata a **risoluzione ridotta** (livelli della wavelet) invece che rifiutata. Corpus: 19 file con
JPX, nessun errore di decodifica, un avviso (0000190: `/ColorSpace` non coerente con i componenti → si
usa quello del file, come pdftoppm); le pagine controllate escono come in pdftoppm.
Fuzzer nuovi: `fuzz_ccitt`, `fuzz_jbig2` (timeout trovati e corretti: dati esauriti, griglia halftone
0×1,8 miliardi, budget), `fuzz_jpx` (5 min, ~42.000 esecuzioni, pulito). La campagna lunga su render ha
trovato un **heap-buffer-overflow in Blend2D** con CTM attorno a 10¹⁶: corretto nel nostro confine
(ritaglio in doppia precisione oltre ±10⁶ pixel), con file di regressione. Modulo WASM 2,52 MB,
866 KB gzip (+71 KB gzip per OpenJPEG; CCITT e JBIG2 +33 KB).

**6g (2026-09-28):** DCTDecode con **libjpeg-turbo 3.2.0** ([ADR 0003](adr/0003-jpeg.md), sostituibile
in futuro): `ExternalProject` con hash e firma GPG verificati, senza SIMD (stessi pixel in nativo e WASM),
ponte C (`filters/jpeg_shim.c`) perché i `longjmp` di libjpeg non attraversino il C++; limiti su pixel,
memoria e scansioni progressive; `/ColorTransform`; CMYK come memorizzato (verificato sul corpus: uguale
a pdftoppm). Fixture generate da `tests/data/jpeg/make_fixtures.py` (grigio, RGB, progressivo, CMYK
Adobe, troncato), fuzzer `fuzz_dct` (3 min, ~153.000 esecuzioni, pulito). Le ombre sfumate di Kensington
ora escono come in pdftoppm. Modulo WASM 2,21 MB, 761 KB gzip (+85 KB).
SafeDocs (build ASan, libjpeg compresa): 2.232 pagine, 0 crash, 0 timeout, nessun errore dei sanitizer.
Sulle prime pagine con immagini **nessun JPEG resta non decodificato** (prima: 464 pagine su 688);
restano CCITT (46 pagine), JPX (13), JBIG2 (6) e 3 immagini oltre il limite di 16 Mpixel.

**6f (2026-09-28):** quello che 6e lasciava fuori. **Funzioni PDF** (`content/function`, §7.10): tipi 0
(campionate, 1–32 bit, interpolazione multilineare), 2, 3 (con profondità limitata: una funzione che
contiene sé stessa si ferma) e 4 (calcolatore PostScript: compilato in un programma senza cicli, pila di
100, errori a run-time → zeri nel range, come i lettori); fuzzer `fuzz_function` (2 min, ~6,2 milioni di
esecuzioni, pulito). Usate per le **tinte Separation/DeviceN** (spazio alternativo + trasformazione: il
colore vero invece dell'approssimazione "più inchiostro, più scuro") e per **`/TR` delle soft mask**
(tabella di 256 valori). **Rasterizzatore:** i livelli di clip partono da una copia di ciò che sta sotto e
finiscono interpolando attraverso la maschera — così i modi di fusione dentro un clip su tracciato vedono
la pagina (prima si fondevano col trasparente: difetto di 6e trovato rileggendo il codice, ora con test);
**gruppi non isolati** composti sulla copia del backdrop con un gemello A8 dell'alfa del gruppo, e la
rimozione del backdrop di §11.4.8 alla fine (una pre-scansione della lista li tratta da isolati quando
dentro non ci sono modi di fusione: stesso risultato, niente copie); **knockout** (§11.4.9): ogni oggetto
si compone col backdrop iniziale del gruppo e sostituisce il contenuto in proporzione alla sua forma
(alfa dell'oggetto sull'alfa costante; dentro un clip su tracciato gli oggetti contano come uno).
**Immagini** (`content/image`): campioni da 1 a 16 bit in ogni spazio colore noto, `/Decode`, Indexed,
maschere stencil col colore di riempimento, `/SMask` (anche di altra misura, e `/Matte`), `/Mask` a chiave
di colore o esplicita, immagini inline con le abbreviazioni; decodificate una volta per pagina, limiti per
immagine (16 Mpixel) e per pagina (64 Mpixel). Il rasterizzatore le disegna come pattern sul quadrato
unitario (nearest se ingrandite senza `/Interpolate`, bilineare altrimenti; molto rimpicciolite, prima
mediate a blocchi, senza moiré); l'SVG le incorpora come PNG. Un'immagine non decodificata dentro il
gruppo di una soft mask non disegna il segnaposto (diventerebbe una maschera piena: un'ombra come
rettangolo nero). SafeDocs: 2.232 pagine, 0 crash, 0 timeout, 1.268 con immagini. **Il limite ora sono i
codec:** sulle prime pagine con immagini, 464 su 688 contengono JPEG non ancora decodificabili (CCITT 46,
JPX 13, JBIG2 6) — anche le ombre sfumate di file come Kensington sono soft mask fatte di JPEG. Modulo
WASM 1,94 MB, 676 KB gzip.

**6e (2026-09-28):** nella lista dei comandi il modo di fusione di ogni pittura e i comandi
`BeginGroup`/`EndGroup` (alpha, modo, isolato, knockout) e `BeginSoftMask`/`EndSoftMask`/`ClearSoftMask`
(il gruppo della maschera come comandi, disegnato con la CTM del `gs` e per il resto lo stato iniziale;
`/BC` nello spazio colore del gruppo). L'interprete legge `/BM` (nome o array: il primo noto) e `/SMask`
dagli ExtGState, e le form con `/Group /S /Transparency` (dentro, alpha, modo e maschera ripartono dai
valori iniziali, §11.6.6). `render/blend`: le 16 funzioni di fusione e la formula di composizione di
§11.3, testate sui valori. Il rasterizzatore compone gruppi (livello proprio; un gruppo "semplice" —
alpha 1, Normal, senza maschera, non isolato — si disegna direttamente, come vuole la specifica), soft
mask (alpha o luminosità su un livello, poi A8 limitata al clip) e le pitture sotto maschera o con modo
non separabile (livello del riquadro della pittura, composto pixel per pixel e rimesso attraverso il
clip rettangolare). Limiti: livelli vivi e area totale dei livelli (4 pagine), oltre i quali si
ripiega (clip al riquadro, gruppo disegnato direttamente). SVG: `mix-blend-mode`, gruppi con opacità e
isolamento, `<mask>` di luminanza o alfa.
**Trovati per strada:** (1) **senza JIT Blend2D non implementa i modi separabili** (WASM, sanitizer,
fuzzing: la pittura fallisce e non disegna nulla) — un test che confronta con la formula l'ha scoperto;
ora si verifica una volta a runtime e dove mancano compone il motore (nativo con JIT: entro 3/255 dalla
formula, provato in release). (2) La pagina si compone su **fondo trasparente**, poi sulla carta bianca:
un modo di fusione non agisce sul bianco della carta (Hue su "bianco" darebbe bianco) — lo mostrava il
confronto con MuPDF e Poppler; ora sul seme di prova coincidiamo con Poppler entro 1/255 in ogni punto
campionato. Il seme `fuzz/corpus/render/transparency.pdf` copre modi, maschere e gruppi; `fuzz_render`
2 min, ~114.000 esecuzioni, pulito. SafeDocs: 141 pagine (87 file) con gruppi o soft mask, 2.232 pagine
disegnate, 0 crash, 0 timeout. Knockout, gruppi non isolati, `/TR` e maschere fatte di immagini: in 6f.
Modulo WASM 1,84 MB, 638 KB gzip.

**6d (2026-09-28):** Blend2D e AsmJit come dipendenze fissate (FetchContent, SHA-256, `SYSTEM`: i loro
header non passano per i nostri `-Werror`); JIT solo nelle build native di rilascio, senza JIT in WASM,
sanitizer e fuzzing. `render/rasterize`: la lista di comandi → bitmap RGBA su pagina bianca, con quello
che a Blend2D manca fatto da noi: clip su tracciati (livello + maschera A8 composta al `Q`; rettangoli
allineati con il clip nativo; oltre `max_layers` si ripiega sul riquadro), trattini (`graphics/dash`:
curve appiattite, schema che riparte a ogni sottotracciato, tetto ai segmenti), linee mai più sottili di
un pixel (come i lettori di riferimento). Limiti su pixel, lato, livelli e trattini; il runtime di
Blend2D inizializzato esplicitamente (in WASM il linker lo toglieva: crash alla prima allocazione).
`render/encode_png` (zlib, filtro Up). `cpdf render [--dpi N] file pagina out.png`, API C
`capriole_document_page_render` (RGBA, tetto 50 Mpixel), e nell'Inspector il selettore
**vettoriale / raster** (canvas, dpi secondo zoom e densità dello schermo). `fuzz_render` ora disegna
anche ogni pagina a 18 dpi, con l'invariante "bitmap opaca delle dimensioni giuste" (2 min, ~156.000
esecuzioni, pulito). Modulo WASM: 1,80 MB, **625 KB gzip** (motore con interprete e rasterizzatore).
Sul corpus SafeDocs (prime 3 pagine di ogni file, build ASan, 72 dpi): **2.232 pagine disegnate, 0
crash, 0 timeout** (le 12 non disegnate sono i 4 file già noti che non si aprono: un file con password,
due non-PDF, un albero delle pagine illeggibile). In release le pagine più pesanti (100.000–193.000
comandi, 2.600 clip) si disegnano in 0,2–0,7 s. Confronto pixel con pdftoppm sulle pagine solo
vettoriali (dove il confronto ha senso finché testo e immagini non ci sono): mediana 0,8% di pixel
diversi (> 32), 6 su 10 sotto l'1%; il massimo (5%) sono tratteggi di linee sottilissime che noi portiamo
a un pixel pieno e Poppler disegna più chiare — da tarare (alpha proporzionale allo spessore) insieme alla
regola delle linee sottili.

**6b (2026-09-27):** `content/interpret_page` → `graphics::DisplayList`: stato grafico e sua pila
(q/Q, cm, stili di linea, ExtGState: spessori, trattini, alpha, font), tracciati, pittura e clipping
(W applicato dopo l'operatore di pittura, §8.5.4), spazi colore (Device*, Cal*, ICCBased per numero di
componenti, Indexed con la sua tabella, Lab; Separation/DeviceN approssimati finché non ci sono le
funzioni PDF), testo posizionato con le larghezze del font (`/Widths`, `/W` di Type0 con CMap Identity,
Courier esatto, gli altri standard 14 stimati), form XObject (matrice, BBox, risorse proprie, profondità
limitata e cicli tagliati), immagini e shading collocati. Scatole di pagina e `/Rotate` ereditati.
Tollerante come un visualizzatore: un'operazione malformata si salta con un avviso, mai un errore di
pagina; limiti su operazioni, comandi, form annidate e q annidati. `graphics/svg`: la lista come SVG
(ASCII, testo del file sempre escapato; testo con codifica non standard come riquadri dei glifi alle
posizioni vere, immagini come segnaposto). `cpdf render`, fuzzer `fuzz_render` (documento → pagine →
SVG: 10 min, ~720.000 esecuzioni; dopo l'ottimizzazione della memoria ha trovato un comportamento
indefinito — `4000000000 j` convertiva in `int` prima di limitare a 0–2 — corretto, con test e seme di
regressione, poi ~400.000 esecuzioni pulite), API C `capriole_document_page_preview_json`, e
nell'Inspector l'**anteprima vettoriale** di ogni pagina con zoom e avvisi.
**Memoria della lista (ottimizzata subito, su richiesta):** comandi grandi e rari (testo, immagini,
shading) in un `Boxed<T>`, tracciati compatti (1 byte per verbo + solo i punti necessari), colori e stili
in tabelle referenziate per indice: un comando da 424 a **120 byte**; la pagina più pesante di SafeDocs
(193.000 comandi) da 160 a **88 MB**, stesso tempo (0,7 s in release).
Sul corpus: SafeDocs 2.232 pagine e x008 2.512 pagine interpretate, **0 crash, 0 timeout**. Avvisi più
frequenti (= il lavoro dei prossimi passi): font standard senza `/Widths` (servono le metriche AFM dei 14
font), colori spot, trasparenze, shading, pattern. Differenze viste confrontando con pdftoppm: linee più
sottili di un pixel (i lettori le disegnano a 1 px: regola del rasterizzatore, 6d), annotazioni non
ancora disegnate (widget di firma), testo nel font generico.

**6a (2026-09-27):** `content/operators` (tutti i 71 operatori della Tabella A.1, con categoria e
numero di operandi) e `content/ContentParser`: operandi come oggetti, operatori come keyword; immagini
inline (`BI … ID dati EI`) come una sola operazione, con la fine dei dati trovata come fanno i lettori
di riferimento (EI tra spazi, seguito da contenuto non binario) o dalla `/L` di PDF 2.0. Modo tollerante
per i content stream rotti: salta il byte dell'errore, e si ferma — invece di ripartire — su un errore
che arriva a fine input (niente comportamento quadratico su input ostile; test con 200.000 `(`).
`page_content` unisce i content stream di una pagina. `cpdf content <file> <pagina>`, fuzzer
`fuzz_content` (5 min, ~930.000 esecuzioni, 0 problemi), API C `capriole_document_page_content_json`,
e nell'Inspector le operazioni della pagina, indentate per q/Q, BT/ET e marked content.
Sul corpus: SafeDocs, 2.232 pagine (le prime 3 di ogni file) lette **tutte pulite** (nessun errore
saltato, nessun operatore sconosciuto); x008, 2.552 pagine: 0 crash, 0 timeout — gli errori sono tutti a
monte (oggetti illeggibili, filtri immagine della fase 6, nomi di filtro con maiuscole storpiate:
`/flateDeCoDE`, candidato per una riparazione della fase 5).

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

## Fase 7 — Estrazione del testo con provenienza (~1–2 mesi) — ✅ completata (2026-10-01)

Glifi → parole → righe → blocchi/colonne; ogni elemento conserva la provenienza (content stream,
indice dell'operatore, oggetto). Base del modello alto, della ricerca, della redaction e della
selezione. Uscita: confronto con `pdftotext`/`mutool draw -F txt` su campione.

**Criterio di uscita fissato (2026-09-30, il più severo dei tre proposti):** su 1.000 pagine, ≥ 95%
delle pagine devono superare **entrambe** le misure **rispetto allo stesso riferimento** — **contenuto**:
le parole come multinsieme, F1 ≥ 0,98; **ordine di lettura**: somiglianza della sequenza di parole
(difflib, 2·corrispondenze / totale) ≥ 0,90. Testo normalizzato NFKC, parole separate dagli spazi.

Passi: **7a** da glifo a testo (ToUnicode, nomi dei glifi, collezioni CJK, cmap del font; `/ActualText`;
provenienza e riquadro di ogni glifo) ✅ · **7b** layout (parole, righe, blocchi, ordine di lettura) ✅ ·
**7c** interfacce (API C, WASM, Inspector con provenienza) ✅ · **7d** confronto ufficiale su 1.000 pagine ✅.

**7d (2026-10-01) — criterio di uscita della fase 7: raggiunto.** Giro ufficiale di
`corpus-tools/compare_text.py` sulla build release, rapporto in
`CORPORA/reports/text-safedocs-0000-20261001-141402/`:

| Campione | Pagine | Passano (contenuto ≥ 0,98 e ordine ≥ 0,90, stesso riferimento) | Strette | I riferimenti concordano fra loro |
|---|---|---|---|---|
| SafeDocs 0000, seme `capriole-phase-7` | 1.000 | **951 (95,1%)** | 517 (51,7%) | 521/1.000 (52,1%) |

Media del riferimento migliore: contenuto 0,993, ordine 0,988; nessun fallimento né timeout di `cpdf`;
133 pagine escluse perché nessun riferimento vi trova testo. Il giro di prova del 30 settembre dava 953:
le due pagine di differenza (0000910, 0000752) sono `|` e `~` scritti in UCS-2 (`UniJIS-UCS2-H`), che
leggiamo come i caratteri che il file contiene mentre i riferimenti passano per il CID della collezione
Japan1 e restituiscono `¦` e una tilde combinante: scelta mantenuta. Restano per le fasi successive le
cause elencate sopra (font senza informazioni, scritture complesse e bidirezionali, CJK verticale, moduli
disegnati fuori ordine).

**7c (2026-10-01).** `capriole_document_page_text_json(document, page, order, &json)`: testo semplice,
blocchi/righe/parole come intervalli con riquadro, ogni carattere con riquadro, corpo, angolo,
invisibilità e provenienza (stream, offset e ordinale dell'operatore, byte del codice, font); JSON ASCII
(testo in `\u`), formato in `capi/README.md`; esportata nel modulo WASM (smoke test). Inspector: sopra
l'anteprima (vettoriale o raster) il **livello di testo** — parole trasparenti e selezionabili nell'ordine
di lettura, posizionate in frazioni della pagina e dimensionate in unità del contenitore (seguono lo zoom)
—, l'ordine (contenuto / colonne), i riquadri delle parole a richiesta; la parola selezionata mostra da
quale operatore e da quale font viene, e l'operatore si evidenzia nel contenuto della pagina (form
XObject e font sono link all'oggetto). Test e2e del percorso parola → operatore.

**7a + 7b (2026-09-30).** Modulo nuovo `text/` (`capriole/text/text.hpp`, ADR 0007).
- *Da glifo a testo* (`Font::text`, §9.10.2): `/ToUnicode` per tutti i font (anche Type 3); per i font
  semplici il nome del glifo (encoding, `/Differences`, altrimenti il nome nel programma) attraverso
  l'Adobe Glyph List, poi il codice stesso se stampabile; per i compositi con CMap Unicode predefinita
  (`Uni…-UCS2/UTF16/UTF32/UTF8`) il codice è il carattere; poi la collezione (Japan1, GB1, CNS1, Korea1:
  la CMap UTF-16 della collezione invertita, preferendo l'ideogramma al radicale Kangxi), poi la cmap
  del programma incorporato. Niente di tutto questo: U+FFFD (come MuPDF). Legature latine espanse.
  Costa solo quando serve: `interpret_page(…, Purpose::kText)` calcola il testo dei glifi (e non
  decodifica le immagini); il disegno non chiede risorse che servono solo al testo.
- *`/ActualText`* (§14.9.4) delle sequenze marcate (in linea o in `/Properties`, stringhe di testo
  UTF-16/UTF-8/PDFDocEncoding — `model/text_string.hpp`): sostituisce i glifi della sequenza; vuoto li
  nasconde.
- *Provenienza* di ogni carattere: lo stream (la pagina, o il form XObject / la procedura di glifo), offset
  e ordinale dell'operatore, byte del codice, font. *Riquadro* dalla larghezza del glifo e da
  ascendente/discendente del font, nelle coordinate della pagina come la si vede (CropBox, /Rotate).
  Testo invisibile (modo 3/7) estratto e marcato; fuori dalla CropBox scartato (come pdftotext).
- *Layout*: parole dagli spazi e dalle distanze (0,15 em misurati dalla larghezza del glifo, senza `Tc`,
  come MuPDF; indietro fino a 1 em resta la stessa parola), doppioni sovrapposti (grassetto simulato)
  tolti; righe e blocchi **nell'ordine del contenuto**, uniti dove la geometria continua (la riga a
  destra sulla stessa linea di base, la riga sotto nello stesso blocco). L'ordine a colonne (regole di
  Breuel) resta come opzione (`--order layout`): misurato, perde (70% contro 95%).
- `cpdf text [--words] [--order content|layout] <file> <pagina>`; `corpus-tools/compare_text.py` (la
  misura del criterio); fuzz target `fuzz_text` (5 minuti, 200.000 esecuzioni, pulito).

Misura sul campione di SafeDocs 0000 (una pagina per file scelta per hash, poi una seconda pagina dai
file con più pagine fino a 1.000 pagine con testo; seme `capriole-phase-7`):

| | Passano | Strette (entrambi) | I riferimenti concordano fra loro |
|---|---|---|---|
| Ordine del contenuto (default) | **953 (95,3%)** | 519 (51,9%) | 521 (52,1%) |
| Ordine a colonne (`--order layout`, 150 pagine) | 105/150 (70,0%) | | |

Il criterio è severo per costruzione: i due riferimenti concordano fra loro solo su metà delle pagine
(pdftotext riordina per colonne e toglie i doppioni; mutool segue il contenuto e li tiene). Le pagine
che non passano: font senza informazioni sul testo (i riferimenti emettono codici grezzi), scritture
complesse (devanagari, arabo in ordine visivo), testo verticale CJK (mutool lo spezza per carattere,
noi teniamo le parole), moduli disegnati fuori ordine (pdftotext li riordina meglio).

## Fase 8 — Prime funzioni utente e app — 🔜 in corso

In ordine di valore/rischio: operazioni sulle pagine (unisci, dividi, riordina, ruota) → annotazioni
→ moduli AcroForm → allegati/Factur-X → redaction reale → firma PAdES → PDF/A → **editing del testo**.
In parallelo nasce la **UI web** (Svelte 5, ADR 0008) che usa il modulo WASM in un Worker; poi la shell
desktop (Tauri, ADR 0008).

Passi, ciascuno con il suo criterio di uscita: **8a** operazioni sulle pagine nel motore ✅ · **8b** web
app `apps/capriole` (miniature, riordino, rotazione, eliminazione, unione, estrazione; il client del
motore in `packages/engine-client`) ✅ · **8c** shell desktop Tauri `apps/desktop` · 8d annotazioni · 8e
moduli AcroForm · 8f allegati e Factur-X · 8g redaction reale · 8h firma PAdES · 8i PDF/A · 8j editing del
testo. I criteri dei passi da 8d in poi si fissano all'avvio di ciascuno.

**8a (2026-10-01) — operazioni sulle pagine: uscita raggiunta.** `edit/pages.hpp`:
`rotate_pages` (multipli di 90°, sopra la rotazione ereditata; tocca solo le pagine),
`arrange_pages` (nuovo ordine: omesse = eliminate, ripetute = duplicate, con le annotazioni copiate
tranne widget e popup), `import_pages` (copia profonda da un altro documento, rinumerata, ogni oggetto
una volta; gli stream lasciano il file sorgente — decifrati, ancora codificati —; i riferimenti a pagine
non importate diventano null, le destinazioni per nome cadono, struttura dei tag e articoli no; i campi
modulo entrano nell'`/AcroForm` rinominati `nome_2`… se il nome è preso, con le risorse predefinite
della sorgente). Riordino e import riscrivono l'albero piatto sotto la radice esistente, cancellano i
nodi intermedi e portano sulle pagine spostate gli attributi ereditati; le pagine già sotto la radice
non si toccano (salvataggio incrementale minimo). Le modifiche si rifiutano su un documento che ha avuto
bisogno di riparazioni: prima un salvataggio completo (l'app lo farà all'apertura).
Writer: `SaveOptions::drop_unreachable` (salvataggio completo dei soli oggetti raggiungibili: estrarre una
pagina da 394 scrive 30 oggetti invece di 5.000), e quattro difetti del salvataggio completo di file
danneggiati trovati dal fuzzer delle pagine e corretti, con test: radice dell'albero mancante (ora un
nuovo oggetto, e il catalogo vi punta), catalogo che è anche l'unica pagina (la pagina prende un numero
nuovo), riferimenti a oggetti assenti (scritti null: nessun oggetto aggiunto dopo può prenderne il
numero), numeri d'oggetto lontanissimi (la tabella xref elencava ogni numero fino al più alto — gigabyte
per un file di 341 byte: ora sottosezioni; `/Size` uno oltre l'ultimo definito). E un `/Size` ostile non
fissa più il prossimo numero d'oggetto; le note di riparazione ripetute si elencano una volta.
`cpdf rotate|arrange|delete-pages [--incremental]`, `extract`, `split`, `merge`; API C
`capriole_document_pages_rotate|arrange|import`, `capriole_document_save_compact` (anche nel modulo WASM);
fuzz target `fuzz_pages` (operazioni scelte dai byte, salvataggio completo e incrementale, rilettura:
dopo le correzioni 368.000 esecuzioni in 15 minuti, pulito).
Criterio: `corpus-tools/check_pages.py` su 200 file di SafeDocs 0000 (seme `capriole-phase-8a`): ruota
una pagina (anche incrementale) — rotazione letta da Poppler, e ruotandola indietro i pixel tornano gli
stessi —, riordina tre pagine, estrae una pagina, unisce: `qpdf --check` mai peggiore dell'originale e
pagine identiche a quelle di partenza (pdftoppm, somiglianza ≥ 0,99). **200 / 200 file**; rapporto in
`CORPORA/reports/pages-safedocs-0000-20261001-161458/`.

**8b (2026-10-02) — la web app.** `packages/engine-client`: il client del motore (worker WASM, tipi
del JSON, script che copia il modulo) uscito dall'Inspector, condiviso con un alias di SvelteKit
(nessun pacchetto npm collegato); nuove richieste: `openForEditing` (un file che ha richiesto
riparazioni è salvato per intero e riletto in memoria prima di modificarlo, e le riparazioni tornano
all'interfaccia), `pagesRotate`, `pagesArrange`, `importFile`, `extract` (su una copia: il documento
aperto non cambia), `saveCompact`. `apps/capriole` (Svelte 5, SvelteKit statico, lo stack
dell'Inspector): apertura con trascinamento, griglia di miniature disegnate dal motore quando entrano
in vista, selezione (clic, Ctrl/Cmd, Maiusc, Ctrl/Cmd+A), trascinamento per riordinare (la posizione
d'arrivo dal puntatore e dalla pagina più vicina, anche negli spazi), frecce della barra per riordinare
da tastiera, ruota, duplica, elimina (non tutte), inserisci un altro PDF dopo la selezione, estrai,
annulla (copie intere del documento, fino a 256 MB), salva (compatto, con /ModDate); avviso se il file
era danneggiato; conferma prima di chiudere o lasciare la pagina con modifiche. Il gate costruisce e
verifica entrambe le app. Criterio: test e2e dei flussi (ruota, elimina, annulla, sposta, trascina,
inserisci, estrai, salva — i byte salvati letti per verificare ordine e rotazione —, file danneggiato):
3/3 verdi, tutto nel browser.

---

## Decisioni aperte (con la fase in cui diventano bloccanti)

| Decisione | Blocca | Nota |
|---|---|---|
| Licenza di Capriole | pubblicazione, OSS-Fuzz, contributi | proposta da discutere: Apache-2.0 o MPL-2.0 |
| ~~Skia vs Blend2D~~ | fase 6 | **deciso: Blend2D** (ADR 0002) |
| ~~Decodificatore JPEG (DCT)~~ | fase 6 | **deciso: libjpeg-turbo** (ADR 0003), sostituibile |
| ~~Gestione del colore (ICC)~~ | fase 6 | **decisa: Little CMS + profilo SWOP** (ADR 0006) |
| ~~JPEG 2000 (JPX)~~ | fase 6 | **deciso: OpenJPEG 2.5.4** (ADR 0004), sostituibile |
| ~~JBIG2 / CCITT con licenza compatibile~~ | fase 6 | **fatto: implementazione propria** |
| ~~Ordine di lettura del testo~~ | fase 7 | **deciso: ordine del contenuto** (ADR 0007), misurato |
| Libreria crittografica per PAdES | fase 8 | mbedTLS vs OpenSSL 3 (entrambe Apache-2.0) |
| Firma qualificata nel browser | fase 8 | smart card non accessibili dal web → desktop o firma remota |
| ~~Framework UI / shell desktop~~ | fase 8 | **decisi: Svelte 5 + Tauri** (ADR 0008) |
| Package manager (vcpkg?) | fase 6 | quando le dipendenze native diventano 5+ |
| Marchio/dominio/GitHub org | pubblicazione | non tecnico |
