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
| **Dopo la fase 8** (🔜 da pianificare insieme: la prossima fase) | | |
| Editing di paragrafi con a capo (reflow) | ⬜ | CAPRIOLE.md §5.4 |
| Font: estendere i sottoinsiemi incorporati | ⬜ | CAPRIOLE.md §5.4 |
| Modello alto del documento (vista semantica) | ⬜ | da valutare, CAPRIOLE.md §5.2 |
| Firma con smart card (desktop), revoca OCSP/CRL | ⬜ | CAPRIOLE.md §6.1 |
| Segnalazione anonimizzata dei file problematici | ⬜ | CAPRIOLE.md §8 |
| Distribuzione: Flathub, hosting web, self-hosted | ⬜ | CAPRIOLE.md §3 |
| Pubblicazione: preparazione (licenze nei binari e nelle app, file pubblici, kit OSS-Fuzz, verifica) | ✅ | `docs/PUBBLICAZIONE.md` |
| Pubblicazione: repo pubblico, OSS-Fuzz | ⏸ | la decide il titolare; prima: il contatto (`<<CONTACT-TBD>>`) |
| Togliere lo skip del passo Binaryen `constraint-analysis` | ⬜ | al prossimo aggiornamento di emsdk |
