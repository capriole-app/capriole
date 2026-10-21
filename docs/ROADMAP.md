# Stato del piano

Quadro d'insieme, aggiornato a ogni passo (il dettaglio di ogni fase è in `PLAN.md`).
Legenda: ✅ fatto · 🔜 prossimo · 🟡 parziale · 🔁 continuo · ⏸ in attesa di una decisione · ⬜ da fare.

| Fase / tema | Stato | Note |
|---|---|---|
| 1 Scheletro del repo | ✅ | |
| 2 Lexer, parser rigoroso, modello basso, cifratura | ✅ | |
| 3 Writer (completo, incrementale, ricifratura) | ✅ | |
| 4 Harness sul corpus | 🟡 | criterio «10.000 file in < 1 h» non ancora segnato come raggiunto |
| 5 Strato di riparazione | 🔁 | guidato dai report |
| 6 Interprete e renderer | ✅ | |
| 7 Estrazione del testo con provenienza | ✅ | |
| Inspector (app di debug) | ✅ | |
| 8a–8c Pagine, web app, shell desktop | ✅ | |
| 8d Annotazioni | ✅ | |
| 8e Moduli AcroForm | ✅ | |
| 8f Allegati e Factur-X | ✅ | |
| 8g Redaction reale | ✅ | |
| 8h Firma PAdES (.p12) | ✅ | |
| 8i PDF/A | ✅ | |
| 8j-1 Modifica del testo nel motore | ✅ | |
| 8j-2 «Modifica testo» nell'app | ✅ | chiude la fase 8 |
| Licenza | ✅ | 0BSD (ADR 0009) |
| **Fase 9 — editing avanzato del testo** | | |
| 9a-1 Reflow dei paragrafi nel motore | ✅ | 100/100 paragrafi del corpus |
| 9a-2 «Modifica testo» › Paragrafo nell'app | ✅ | |
| 9b Paragrafi con più stili | ✅ | 100/100, 3681 parole con il loro stile |
| 9d-1 Spostare ciò che segue, motore | ✅ | 100/100 spostati; rifiuti spiegati (soprattutto pagine piene) |
| 9d-2 «Sposta in basso ciò che segue» nell'app | ✅ | e2e: spostato, piè di pagina fermo, salvato, annullato; rifiuto in italiano |
| 9c-1 Sottoinsiemi dei font estesi, motore | ✅ | 42 pagine (16 dei corpora, 26 generate) |
| 9c-2 Font forniti nell'app | ✅ | e2e: suggerimento, aggiunta, salvataggio, memoria |
| **Fase 10 — modello alto della pagina e oggetti** | | CAPRIOLE.md §5.2 |
| 10a Modello della pagina nel motore | ✅ | 100/100 pagine: copertura e provenienza; 9a/9b/9d invariati |
| 10b Spostare, ridimensionare, eliminare immagini e disegni (motore) | ✅ | 100/100 oggetti (Capriole, Poppler, MuPDF) |
| 10c «Seleziona e sposta» su immagini e disegni nell'app | ✅ | e2e: sposta, ridimensiona, elimina, salva, annulla, rifiuto |
| **Fase 11 — firma con smart card e a lungo termine** | ✅ | CAPRIOLE.md §6.1 |
| 11a Firma con una chiave fuori dal motore | ✅ | 100/100 file firmati con le chiavi su un token PKCS#11 (SoftHSM); RSA identica byte per byte al `.p12` |
| 11b Revoca OCSP/CRL e firme B-LT, B-LTA nel motore | ✅ | 100/100 file B-LTA: pyHanko, Capriole dai soli dati del file, pdfsig; il revocato revocato per tutti |
| 11c Smart card e rete nell'app desktop | ✅ | SoftHSM nei test della shell; e2e con la shell simulata: B-LTA valido per pyHanko e pdfsig. Nessuna carta reale provata |
| Rifiniture prima del rilascio: 5 lingue, zoom, marchio, «Informazioni» | ✅ | traduzioni FR/DE/ES da far rivedere a madrelingua |
| Segnalazioni A: «Segnala un problema» (senza file) | ✅ | dialogo con l'anteprima esatta di ciò che parte; servizio `capriole-reports` su contabo-b (16 KB, limiti di frequenza, tetto giornaliero di 1000 o 8 MiB, nessun IP, 12 mesi); condizioni d'uso aggiornate (versione del 2026-10-14); solo nell'app web |
| Segnalazioni B: il file problematico anonimizzato | ⬜ | CAPRIOLE.md §8: il motore isola gli oggetti dell'errore, sostituisce testo, immagini, font, metadati; verifica che l'errore resti; consenso caso per caso |
| Impostazioni, colori predefiniti, firma grafica | ✅ | stamp uguale in Capriole, Poppler, MuPDF |
| Distribuzione 12a: web app su capriole.app (contabo-b) | ✅ | online dal 2026-10-13: header verificati da fuori (HSTS, CSP, redirect da `www`); aperto ai motori di ricerca (niente più `noindex`, gateway ricaricato e verificato da fuori; titolo, descrizione, canonical, Open Graph, `robots.txt`, `sitemap.xml`); rilasci automatici da Jenkins (`./deploy.sh`) riusciti; runbook in `docs/DEPLOY.md`; registrato su Google Search Console, sitemap inviata |
| Condizioni d'uso (Bobolar Ltd, legge irlandese) | ✅ | online dal 2026-10-14, verificate da fuori; 5 lingue, prevale l'inglese; accettate con una spunta prima di aprire un file, chieste di nuovo quando cambiano; privacy: nessun IP registrato; e2e. **Da far rivedere a un legale** |
| Conteggio anonimo delle visite | ✅ | attivo dal 2026-10-14: aperture della pagina per giorno, senza IP né header (`deploy/web/stats.sh`, cron dell'utente di rilascio alle 00:15, registro `~/capriole-usage.csv`); conta a parte le persone (l’app avviata) e gli scanner che aprono solo la pagina; il gateway non tiene log degli accessi (verificato) |
| Indicizzazione nelle 5 lingue: un URL per lingua (`/it/`, `/fr/`…) con `hreflang` | ✅ | online dal 20/10, verificata da fuori: `/it/`, `/en/`, `/fr/`, `/de/`, `/es/` con lang, titolo, canonical e i 6 `hreflang`; sitemap con 6 URL; `/xx/` dà 404; CSP presente. `/` resta `x-default`. Resta da inviare di nuovo la sitemap in Search Console |
| Revisione dell'interfaccia | ✅ | stile «carta» (chiaro) e «inchiostro» (scuro), lontano da quello di Adobe: superfici piatte, linee sottili, un solo accento (il blu del logo `#2F5BEA`), Inter servito da noi; anteprima approvata il 20/10. Passi 1 fondamenta, 2 barra e griglia con il menu «⋯», 3 dialoghi e pannelli, 4 vista della pagina, gate verde a ogni passo. Online dal 21/10, verificata da fuori (font, 5 lingue, CSP) |
| Distribuzione 12b–c: Windows, Flathub, self-hosted | ✅ | **Capriole 0.1.0 pubblicata** (18/10): desktop Windows e Linux su GitHub Releases, «Scarica» su capriole.app, immagine `capriole-web:0.1.0`; Flathub rinviato — PLAN.md «Distribuzione 12b–c» |
| 12b-1 Shell pronta per Windows (NSIS, ID `app.capriole.Capriole`, moduli PKCS#11 per piattaforma) | ✅ | versione unica controllata dal gate |
| 12b-2 Immagine e build: `.deb`, AppImage, `setup.exe` incrociato | ✅ | `v0.1.0-rc2` provata su Windows 11 (installa, apre, modifica, salva, firma `.p12`); l'installer spiega le cartelle non scrivibili |
| 12b-3 GitHub Release dai tag `v*` | ✅ | pre-release `v0.1.0-rc1` pubblicata da Jenkins (17/10), `SHA256SUMS` verificato |
| 12b-4 «Scarica l'app per il computer» | ✅ | online su capriole.app dal 18/10, link verificati sui file di `v0.1.0` |
| 12b-5 Flathub | ⏸️ rinviato | il manifest si costruisce e passa il linter di Flathub (`manifest` e `repo`), ma la submission viola le regole di Flathub (20/10): niente manifest né PR scritti con l'AI, build dai sorgenti, storia di sviluppo lunga — `packaging/flathub/README.md`. Linux resta coperto da `.deb` e AppImage |
| 12c Self-hosted | ✅ | `docs/SELF-HOSTED.md`, compose provato in locale; `ghcr.io/capriole-app/capriole-web:0.1.0` pubblica |
| Capriole 0.2.0: la nuova interfaccia nelle app desktop e nell'immagine self-hosted | ✅ | pubblicata il 21/10 (tag `v0.2.0`): i 4 file della release scaricati dai link «Scarica» corrispondono a `SHA256SUMS`, `.deb` 0.2.0, `capriole-web:0.2.0` su ghcr; la prova dell'installer su Windows passa alla 0.3.0 |
| Ruotare la pagina dalla vista singola (richiesta degli utenti) | 🟡 | «Ruota a sinistra / a destra» nella barra della vista pagina, con «Annulla»; e2e. Nella 0.3.0 |
| Evidenziare dove non c'è testo vero, come in una scansione (richiesta degli utenti) | 🟡 | evidenzia, sottolinea e barra: sul testo seguono le parole, altrove un rettangolo (un'annotazione standard), come «Oscura»; e2e. L'OCR resta fuori (pesante, CAPRIOLE.md §1). Nella 0.3.0 |
| Capriole 0.3.0: rotazione dalla vista pagina ed evidenziazione sulle scansioni, nella web app e nelle app desktop | 🟡 | web app online dal 21/10 (verificata da fuori: `app: 0.3.0`, «Ruota» nella vista pagina); le due build del tag `v0.3.0` sono fallite perché savannah ha risposto 504 sul download di FreeType: i file desktop e l'immagine arrivano con la 0.3.1 |
| Capriole 0.3.1: i file desktop e l'immagine self-hosted della 0.3.0, FreeType da SourceForge, JBIG2 più veloce | 🟡 | versione 0.3.1 nel repo; da pubblicare con il tag `v0.3.1` su `main` (build del tag avviata a mano), poi l'installer da provare su Windows |
| JBIG2: decodifica generica più veloce (le scansioni a 600 dpi) | ✅ | il giro sui corpora del 21/10 ha dato due timeout su scansioni da 30 milioni di pixel a pagina: il contesto di ogni pixel ora scorre in finestre di bit invece di 16 letture dalla bitmap; 2,2× più veloce, uscita identica byte per byte, test contro la formula di 6.2.5.3 su tutti i template. Resta la decodifica aritmetica (un simbolo per pixel, come jbig2dec e PDFium) |
| Dipendenze: un mirror per FreeType | ✅ | savannah ha dato 504 a due build del tag `v0.3.0` (21/10): lo stesso tarball (stesso SHA-256) da SourceForge, savannah di riserva; `capriole_dependency_url` accetta mirror, `fetch_deps.py` li prova in ordine. Le altre dipendenze vengono da GitHub e npm |
| Pacchetto di risorse su capriole.app | ✅ | servito dal 18/10 (font, CMap, profilo CMYK); prima dava 404 |
| Pubblicazione: preparazione (licenze nei binari e nelle app, file pubblici, kit OSS-Fuzz, verifica) | ✅ | `docs/PUBBLICAZIONE.md` |
| Pubblicazione: repo pubblico | ✅ | dal 2026-10-14 su `github.com/capriole-app/capriole` (0BSD): storia riscritta senza indirizzi personali (autore noreply; `prepublish_check.sh` «Ready to publish»), contatti `security@`/`hello@capriole.app`, link al codice in «Informazioni e licenze»; vecchio repo cancellato; Jenkins sul nuovo repo |
| OSS-Fuzz | ⏸️ rinviato | kit pronto in `ci/oss-fuzz/`; OSS-Fuzz accetta progetti con una base d'utenti significativa o critici per l'infrastruttura (20/10): si riprende quando ci sono utenti. Serve poi un account Google con `security@capriole.app` e la PR a `google/oss-fuzz` (`docs/PUBBLICAZIONE.md`, passo 15) |
| Togliere lo skip del passo Binaryen `constraint-analysis` | ⬜ | al prossimo aggiornamento di emsdk |
