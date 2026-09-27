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
  nice 19, I/O idle; unità systemd utente `capriole-fuzz`). Prima esecuzione (dal 2026-09-26 21:10)
  interrotta dopo ~13,8 h da un esaurimento di memoria **dell'intera macchina** (non del fuzzer, 423 MB
  sotto il suo tetto): `filters`, `header`, `lexer` completati, 4 h ciascuno, **0 problemi**; archivio
  in `CORPORA/reports/fuzz-campaign-20260926-partial/`. Ripresa il 2026-09-27 11:22 sui target mancanti
  (`object`, `security`, `xref`, più il nuovo `serialize`, 4 h ciascuno) con
  `fuzz_local.sh start 16 object security xref serialize`. Esito da registrare qui.

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

## Fase 6 — Interprete dei content stream e renderer (~4–6 mesi, il grosso) — 🟡 in corso

Passi: **6a** parser dei content stream ✅ · **6b** interprete (stato grafico, CTM, tracciati,
clipping, colori → lista di comandi di disegno indipendente dal rasterizzatore) ✅ · **6c** prototipo
Skia vs Blend2D dietro la stessa interfaccia, benchmark e dimensione WASM → decisione 🔜 · **6d** rendering
e confronto pixel con mutool/pdftoppm, anteprima nell'Inspector · poi font, immagini, trasparenza,
shading.

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
