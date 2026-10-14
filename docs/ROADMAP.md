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
| Indicizzazione nelle 5 lingue: un URL per lingua (`/it/`, `/fr/`…) con `hreflang` | ⬜ | oggi una sola pagina statica, i meta in inglese; l'app li traduce solo dopo l'avvio |
| Distribuzione 12b–c: Windows, Flathub, self-hosted | ⬜ | CAPRIOLE.md §3 |
| Pubblicazione: preparazione (licenze nei binari e nelle app, file pubblici, kit OSS-Fuzz, verifica) | ✅ | `docs/PUBBLICAZIONE.md` |
| Pubblicazione: repo pubblico | ✅ | dal 2026-10-14 su `github.com/capriole-app/capriole` (0BSD): storia riscritta senza indirizzi personali (autore noreply; `prepublish_check.sh` «Ready to publish»), contatti `security@`/`hello@capriole.app`, link al codice in «Informazioni e licenze»; vecchio repo cancellato; Jenkins sul nuovo repo |
| OSS-Fuzz | ⬜ | kit pronto in `ci/oss-fuzz/`; serve un account Google con `security@capriole.app`, poi la PR a `google/oss-fuzz` (`docs/PUBBLICAZIONE.md`, passo 15) |
| Togliere lo skip del passo Binaryen `constraint-analysis` | ⬜ | al prossimo aggiornamento di emsdk |
