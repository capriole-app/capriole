# ADR 0009 — Licenza: 0BSD

Data: 2026-10-06 · Stato: **accettata** (decisione dell'utente: «la più permissiva possibile»)

## Contesto

La licenza era una decisione aperta (CAPRIOLE.md §10, PLAN.md «Decisioni aperte»): finché mancava, il
repository restava privato, non poteva accettare contributi ed era escluso da OSS-Fuzz. Le candidate
discusse erano Apache-2.0 e MPL-2.0. L'utente ha chiesto la licenza più permissiva possibile.

## Decisione

Capriole (motore, API C, binding, app, strumenti) è distribuito con la **BSD Zero Clause License
(0BSD, SPDX `0BSD`)**, testo in `LICENSE`.

- Permette uso, copia, modifica e distribuzione, anche commerciale e in prodotti chiusi, **senza alcuna
  condizione**: nemmeno conservare l'avviso di copyright (a differenza di MIT, BSD e Apache-2.0).
- È approvata dall'OSI (requisito di OSS-Fuzz) e, a differenza di una dedica al pubblico dominio
  (Unlicense, CC0), non dipende dal diritto di rinunciare al diritto d'autore, che in Italia e in altri
  ordinamenti non è riconosciuto.
- Detentore: «the Capriole authors» (chi contribuisce accetta la stessa licenza per il suo contributo).

## Conseguenze

- **Nessuna concessione esplicita di brevetti**, a differenza di Apache-2.0: è il prezzo della
  permissività massima. Se servisse in futuro, si può aggiungere Apache-2.0 come seconda licenza a scelta.
- Chiunque può farne una versione chiusa e venderla; un modello di finanziamento basato su licenze
  commerciali non è più possibile sul codice rilasciato così (CAPRIOLE.md §10 lo legava al finanziamento).
- Le dipendenze restano sotto le loro licenze (`docs/DEPENDENCIES.md`), e chi distribuisce i binari
  (WASM, app desktop) deve includerne i testi e gli avvisi: Mbed TLS (Apache-2.0), FreeType (FTL, con
  la sua clausola di citazione nella documentazione), libjpeg-turbo, OpenJPEG, zlib, Little CMS,
  Blend2D, i font (OFL). Da fare prima della prima pubblicazione: una pagina «Licenze» nelle app.
- Il vincolo «nessuna dipendenza copyleft linkata» (CLAUDE.md, principio 7) resta: è ciò che permette
  di distribuire tutto alle stesse condizioni.
- Rendere pubblico il repository resta una decisione a parte.
