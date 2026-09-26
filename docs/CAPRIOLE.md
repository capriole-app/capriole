# Capriole — descrizione del progetto

Documento di riferimento per lo sviluppo. Raccoglie le decisioni prese finora, i vincoli e le questioni ancora aperte. Dove qualcosa è una proposta e non una decisione, è indicato esplicitamente.

## 1. Visione

Una fondazione che sviluppa, in forma open source e gratuita, alternative di qualità professionale ai software commerciali in abbonamento. Il modello di riferimento è la Blender Foundation: focus stretto, un prodotto alla volta, qualità tale che i professionisti lo scelgano e non solo chi vuole risparmiare.

Il primo prodotto è **Capriole**, un editor PDF alternativo ad Adobe Acrobat.

Motivazioni:
- Acrobat è tra gli abbonamenti più odiati e l'editing PDF è il punto in cui le alternative libere sono più deboli.
- Su Linux la situazione è particolarmente scarsa: l'opzione di riferimento per l'editing oltre le funzioni di base è Master PDF Editor, che è closed source e richiede licenza per l'uso commerciale.
- Il PDF resterà centrale per decenni: è standard ISO (ISO 32000), esiste la variante d'archivio PDF/A, le firme qualificate europee PAdES si basano su di esso. Il suo ruolo si sta spostando verso il documento finale (contratti, moduli, atti, certificati, tavole tecniche) e verso il PDF ibrido con dati strutturati incorporati (es. Factur-X/ZUGFeRD, PDF/A-3 con XML).

Il finanziamento della fondazione è volutamente rimandato: ora ci si concentra sulla tecnica.

## 2. Nome e identità

- **Nome:** Capriole (in italiano plurale di capriola, in inglese "capriole" è il salto del cavallo nell'equitazione classica). Pronuncia inglese naturale: *KAP-ree-ohl*.
- **Verifiche ancora da fare prima di adottarlo definitivamente:** marchi EUIPO e USPTO nelle classi 9 e 42, dominio (idealmente `capriole.org`), organizzazione `capriole` su GitHub. Nomi scartati perché già occupati nel software: Funambol, Trapeze.
- **Logo:** una C tracciata come freccia circolare in senso orario (la capriola) con un documento al centro, leggermente inclinato nel verso della rotazione. Wordmark "capriole" in minuscolo.
- **Colori:** blu inchiostro `#111D4A` (C e testo), blu cobalto `#2F5BEA` (documento), carta `#F5F7FC`, notte `#0E1633` (sfondi scuri).
- **Carattere del wordmark:** Bricolage Grotesque (licenza OFL), peso ~750, tracking -0.045em; negli SVG il testo è convertito in tracciati.
- **Rischio noto sul logo:** una freccia circolare attorno a un foglio può ricordare l'icona "ruota pagina / aggiorna" delle interfacce. Da valutare nell'uso reale dentro l'app.

## 3. Piattaforme e architettura di distribuzione

Decisione: **i file dell'utente non lasciano mai il suo dispositivo.**

- **Web app client-side:** il motore in C++ compilato in WebAssembly gira interamente nel browser. Il sito serve solo file statici, quindi i costi di hosting restano quasi nulli anche con molti utenti, e non ci sono problemi di GDPR sui documenti.
- **App desktop:** stesso codice, compilato in nativo e impacchettato (proposta: Tauri).
- **Linux è piattaforma di prima classe fin dal primo giorno**, non un port successivo. Distribuzione proposta: Flathub.
- **Versione self-hosted** per enti e aziende che la vogliono sui propri server.
- Eventuali funzioni che richiedono davvero un server (OCR pesante, collaborazione in tempo reale) sono opzionali e successive.

### 3.1 Conseguenze tecniche (aggiunte in fase di avvio)

- **Il motore espone un'API C stabile** (`capi/`), unico confine verso il binding WASM, la shell desktop (FFI) ed eventuali terzi. Così la scelta dell'interfaccia non vincola il motore.
- **Tauri implica un'interfaccia web.** Il desktop è una webview con la stessa UI del sito; il motore può girarvi come WASM oppure nativo tramite FFI. L'API C tiene aperte entrambe le strade.
- **Il motore non presuppone il file intero in memoria.** In wasm32 la memoria è limitata a 4 GB e i PDF tecnici o scansionati possono essere enormi. L'input passa da un'astrazione ad accesso casuale (`ByteSource`); nel browser è implementata con `FileReaderSync` dentro un Web Worker.
- **Thread in WASM:** richiedono `SharedArrayBuffer`, quindi gli header HTTP COOP/COEP. L'hosting statico deve permetterli (GitHub Pages, ad esempio, non li imposta; serve un altro hosting o un service worker). Lo stesso vale per la versione self-hosted.

## 4. Motore PDF: scritto da noi

Decisione: **motore PDF proprio, in C++.** Motivi:
- L'editor ha comunque bisogno di un proprio modello del documento, ricco e modificabile, che nessuna libreria esistente espone come serve.
- La licenza del progetto resta interamente nelle nostre mani (MuPDF, il candidato tecnicamente migliore, è AGPL o commerciale con copyright di Artifex; PDFium ha licenza permissiva ma API pensate per un visualizzatore e un sistema di build pesante; Poppler è GPL e orientato alla lettura).

### 4.1 Cosa scriviamo noi

Tutto ciò che è specifico del PDF:
- parser (sintassi, xref table e xref stream, object stream, cifratura);
- modello a oggetti di basso livello;
- interprete dei content stream;
- renderer (sopra una libreria di rasterizzazione);
- writer (salvataggio completo e incrementale);
- strato di editing (vedi sezione 5);
- crittografia dei PDF cifrati (Standard security handler R2–R6): MD5, RC4, AES-128/256, SHA-256/384/512. Sono algoritmi piccoli, verificabili con i vettori di test NIST, e scriverli evita una dipendenza nel percorso di lettura.

### 4.2 Cosa riusiamo (librerie con licenza permissiva)

- Compressione: zlib.
- Immagini: libjpeg-turbo (DCT), OpenJPEG (JPEG 2000).
- Font: FreeType (rasterizzazione e parsing), HarfBuzz (shaping per il testo inserito).
- Colore: LittleCMS (profili ICC).
- Rasterizzazione 2D: Skia oppure Blend2D (**da decidere**).
- Crittografia per le firme (PAdES): X.509, CMS/PKCS#7, RSA/ECDSA, marche temporali RFC 3161. Candidati: mbedTLS oppure OpenSSL 3, entrambi Apache-2.0 (**da decidere**; mancava nella prima stesura). Nel browser si valuta anche WebCrypto per le primitive.
- FreeType ha doppia licenza (FTL o GPLv2): si usa sotto **FTL**.

**Attenzione alle licenze delle dipendenze:** finché la licenza di Capriole non è decisa, evitare dipendenze copyleft linkate nel motore. Esempio concreto: jbig2dec (decoder JBIG2 molto diffuso) è di Artifex e AGPL; per JBIG2 e CCITT Fax serve un'implementazione propria o un'alternativa con licenza compatibile. Ogni nuova dipendenza va verificata prima di essere aggiunta.

### 4.3 Dove sta la difficoltà

- **Parser:** difficoltà media come sintassi; il lavoro lungo è la robustezza sui file malformati, molto diffusi nel mondo reale.
- **Writer:** la parte più facile.
- **Font:** Type1, TrueType, CFF, Type3, CIDFont, encoding e CMap, ognuno con le sue anomalie.
- **Rendering:** il grosso del lavoro. Tracciati e clipping sono semplici; gruppi di trasparenza, soft mask, blend mode, i sette tipi di shading, spazi colore Separation/DeviceN sono dove i motori si distinguono. Il 95% dei file è raggiungibile; l'ultimo 5% è la coda lunga.
- **Sicurezza:** il motore legge file non fidati. Fuzzing continuo obbligatorio (sezione 7).

## 5. Architettura del documento a due livelli

Principio cardine: **toccare solo ciò che l'utente modifica.** Il PDF non viene mai rigenerato da zero a partire da una struttura semplificata, perché si perderebbero firme digitali, struttura di accessibilità (tagged PDF), metadati, oggetti rari e ottimizzazioni del file originale.

### 5.1 Modello basso (fedele al file)

- Dizionari, array, stream, riferimenti indiretti, esattamente come nel PDF.
- È la fonte di verità.
- Ogni oggetto tiene traccia del proprio stato (non modificato / modificato / nuovo / eliminato).

### 5.2 Modello alto (vista semantica)

- Costruito sopra il modello basso, non al suo posto.
- Ricostruisce da glifi sparsi una struttura logica: parole, righe, paragrafi, colonne; più immagini, tracciati vettoriali, annotazioni, campi modulo.
- Ogni elemento conserva la **provenienza**: da quali operatori del content stream e da quali oggetti deriva.

### 5.3 Propagazione delle modifiche

Flusso: PDF → modello basso → vista alta → modifica dell'utente → propagazione al modello basso → PDF in cui sono riscritte solo le parti cambiate.

- Una modifica rigenera solo gli operatori della porzione interessata del content stream, preservando lo stato grafico circostante (CTM, clipping, coppie q/Q, stato del testo).
- Solo gli oggetti toccati vengono marcati come modificati.
- Salvataggio preferito: **aggiornamento incrementale** in coda al file originale, così il resto resta identico al byte e le firme precedenti restano verificabili.
- La sezione incrementale riproduce la **forma dell'ultima xref** del file (tabella classica, xref stream o file ibrido con `/XRefStm`): mischiarle rompe alcuni lettori. Se il file è cifrato, gli oggetti nuovi o modificati vanno **ricifrati** con lo stesso handler.

### 5.4 Le due difficoltà principali dell'editing del testo

- **Font:** nei PDF i font sono spesso incorporati solo come sottoinsieme. Se l'utente scrive un carattere assente, bisogna estendere il sottoinsieme (se il font completo è disponibile) oppure scegliere un sostituto con metriche compatibili.
- **Reflow:** il PDF non ha reflow. Allungando una frase, il testo deve andare a capo dentro il paragrafo ricostruito senza spostare il resto della pagina, salvo scelta esplicita dell'utente.

## 6. Priorità funzionali

L'editing libero del testo è il fattore distintivo, ma il grosso dell'uso reale di Acrobat riguarda:
- compilazione di moduli (AcroForm);
- firma digitale (PAdES), senza invalidare firme esistenti;
- oscuramento (redaction) reale, che rimuove il contenuto e non si limita a coprirlo;
- annotazioni;
- unione, divisione, riordino e rotazione delle pagine;
- conversione e validazione PDF/A;
- file allegati e dati incorporati (es. fatture Factur-X/ZUGFeRD).

Il modello a oggetti deve supportare fin dall'inizio salvataggi incrementali e file incorporati, per non doverlo rifare dopo.

### 6.1 Firma digitale: vincoli di piattaforma

La firma PAdES richiede una libreria crittografica (§4.2) e si scontra con il principio "tutto nel browser" in tre punti:

- **Smart card e token USB** (la firma qualificata più diffusa in Italia e in Europa) si raggiungono via PKCS#11 o tramite il middleware del sistema operativo. **Una pagina web non vi ha accesso**, e non esiste un'API standard del browser per farlo. Le alternative sul web sono un'estensione più un'app nativa (fragile, da mantenere per ogni browser) oppure l'app desktop.
- **La firma remota** (certificato qualificato presso un prestatore, con OTP) funziona anche dal browser, ma è una chiamata di rete verso il prestatore. Esce solo l'impronta (hash) del documento, non il documento: il principio del §3 resta rispettato, però va dichiarato all'utente.
- **Marca temporale e validazione a lungo termine** (PAdES-B-T, B-LT, B-LTA) richiedono chiamate a TSA, OCSP e CRL. Molti di questi servizi non inviano gli header CORS, quindi il browser non può interrogarli direttamente: servirebbe un proxy (un server, contro il §3) oppure l'app desktop.

Conseguenza proposta: nella web app, firma con certificato software (.p12) e firma remota; la **firma qualificata con smart card e la validazione completa sono funzioni dell'app desktop.** La verifica delle firme esistenti, invece, può restare nel browser, salvo il controllo di revoca online.

## 7. Robustezza e test

### 7.1 Parser rigoroso + strato di riparazione separato

- Prima si tenta la lettura secondo specifica.
- Se fallisce, entra una modalità tollerante: ricostruzione della tabella xref scansionando il file alla ricerca delle definizioni `N G obj`, correzione delle lunghezze errate degli stream, decompressione parziale degli stream corrotti, gestione dei file troncati.
- I due livelli restano separati nel codice, per evitare che il parser diventi una giungla di eccezioni.

### 7.2 Corpora di test

- **SafeDocs CC-MAIN-2021-31-PDF-UNTRUNCATED** (digitalcorpora.org): quasi 8 milioni di PDF (circa 8 TB) raccolti dal web nel 2021 e riscaricati integri, senza il troncamento a 1 MB di Common Crawl.
- **UNSAFE-DOCS**: oltre 1,5 milioni di PDF malformati costruiti per mettere in crisi i parser, più file troncati utili per testare il recupero.
- **Bug tracker** di pdf.js, PDFium, Poppler, MuPDF e qpdf: file che hanno già causato problemi, uno per bug.

### 7.3 Test differenziale

- Il riferimento per "cosa è corretto" su file difettosi è il comportamento di MuPDF, PDFium e pdf.js (es. se due su tre recuperano 12 pagine e Capriole 10, è un bug).
- Rendering: confronto pixel per pixel delle pagine rese da Capriole e dai motori di riferimento.
- I motori di riferimento sono **strumenti di test esterni, mai dipendenze linkate** (vincolo di licenza).

### 7.4 Regressione e fuzzing

- Ogni bug corretto produce un test permanente: file minimizzato al minimo che riproduce il problema, aggiunto alla suite di regressione.
- Fuzzing guidato dalla copertura (libFuzzer o AFL++) con sanitizer attivi (ASan, UBSan), a partire dai file del corpus. Obiettivo a regime: integrazione in OSS-Fuzz.

## 8. Segnalazione dei file problematici

I file che falliscono sul campo alimentano il miglioramento, **senza violare la privacy**:

1. Il motore, localmente, isola gli oggetti coinvolti nell'errore e scarta il resto.
2. Sostituisce testo, immagini e metadati con segnaposto della stessa dimensione (gli offset in byte restano coerenti).
3. Verifica che il file ripulito riproduca ancora lo stesso errore.
4. Mostra all'utente cosa verrebbe inviato e chiede consenso esplicito, caso per caso.

Note sull'anonimizzazione:
- **Il testo si ricostruisce anche senza le stringhe del content stream**: dai sottoinsiemi di glifi dei font incorporati (quali caratteri esistono e in che ordine sono usati), dalle mappe ToUnicode, dai nomi dei font, dalla struttura dei tag, dai segnalibri, dai nomi dei campi modulo e dagli allegati. Tutti questi elementi vanno ripuliti, non solo il testo visibile.
- **Negli stream compressi la sostituzione avviene sul contenuto decodificato.** Dopo la ricompressione la lunghezza cambia, quindi gli offset si ricalcolano con un salvataggio completo; "stessa dimensione" vale per il contenuto, non per i byte del file. Se l'errore dipende proprio dagli offset originali (xref corrotta), il file ripulito va verificato con particolare cura al passo 3.

Lato sviluppo:
- Un modello AI nella pipeline (non nell'app) raggruppa i casi per tipo di difetto, minimizza, propone correzione e test di regressione; **una persona revisiona sempre prima del merge.**
- L'app resta deterministica: stesso file, stesso risultato.
- **Prompt injection:** un PDF può contenere testo costruito per manipolare il modello che lo analizza. Il contenuto va trattato solo come dato.
- **Avvelenamento del canale:** un attaccante può inviare molti casi costruiti per spingere le correzioni verso comportamenti permissivi sfruttabili. I file ricevuti si processano in sandbox e si trattano come ostili, come l'input del fuzzer.

## 9. Proposta di primi passi (da validare)

Ordine suggerito, in cui ogni fase produce qualcosa di verificabile. Il piano operativo, con i criteri di uscita di ogni fase, è in `docs/PLAN.md`; le decisioni prese all'avvio sono in `docs/adr/`.

1. **Scheletro del repository** (fatto, con C++23 invece di C++20: vedi ADR 0001): CMake, build nativa Linux e build WebAssembly (Emscripten) fin dall'inizio, CI con sanitizer.
2. **Lexer e parser rigoroso** + modello a oggetti di basso livello con tracciamento delle modifiche.
3. **Writer** con salvataggio completo e incrementale. Primo test fondamentale: apri e salva senza modifiche deve produrre un file equivalente; il salvataggio incrementale deve lasciare intatti i byte originali.
4. **Harness di test:** download di un sottoinsieme del corpus SafeDocs, runner che confronta con qpdf/MuPDF/PDFium come strumenti esterni, primo target di fuzzing sul parser.
5. **Strato di riparazione** guidato dai fallimenti sul corpus.
6. **Interprete dei content stream e renderer**, con test differenziale sui pixel.
7. **Estrazione del testo con provenienza** (base del modello alto).
8. Prime funzioni utente: operazioni sulle pagine, annotazioni, moduli; poi editing del testo.

## 10. Questioni aperte

- Licenza di Capriole (legata al modello di finanziamento, per ora rimandato).
- Libreria di rasterizzazione: Skia o Blend2D.
- Implementazione di JBIG2 e CCITT Fax con licenza compatibile.
- Framework per l'interfaccia utente e shell desktop (proposta Tauri, da confermare).
- Esito delle verifiche su marchio, dominio e GitHub per il nome Capriole.
- Libreria crittografica per PAdES: mbedTLS o OpenSSL 3 (§4.2).
- Perimetro della firma nel browser rispetto al desktop (§6.1): da confermare la proposta "smart card solo su desktop".
- Hosting della web app compatibile con gli header COOP/COEP, se il motore userà i thread (§3.1).
- Licenza e contributi: finché la licenza non è decisa, il repository resta privato, non può accettare contributi esterni ed è escluso da OSS-Fuzz, che richiede un progetto open source.
