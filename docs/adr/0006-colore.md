# ADR 0006 — Gestione del colore: Little CMS, profilo di stampa SWOP per il DeviceCMYK

Data: 2026-09-30 · Stato: **accettata**

## Contesto

Fino a qui i colori si calcolavano per formula: RGB e grigi come sono, il CMYK con la conversione
ingenua (1 − c)(1 − k), i profili ICC ignorati (solo il numero di componenti). Il risultato si vede: i
colori CMYK escono **più accesi e saturi di come li mostra Acrobat** (osservazione dell'utente), perché
Acrobat passa il DeviceCMYK per un profilo di stampa (U.S. Web Coated SWOP) e applica i profili
incorporati. Anche pdftoppm si avvicina ad Acrobat: interpola i colori degli angoli del cubo CMYK presi
da una stampa SWOP. Un ciano pieno: noi (0, 255, 255), Acrobat e pdftoppm circa (0, 170, 235).

## Decisione

**Little CMS 2.19.1** (MIT) per i profili ICC: compilato dai sorgenti come zlib, un contesto per
trasformazione con un **budget di memoria** (allocatore nostro, 64 MB), messaggi d'errore silenziati,
senza cache (trasformazioni utilizzabili da più thread). Verso **sRGB**, intento **colorimetrico
relativo con compensazione del punto nero** (come Acrobat mostra a schermo):
- `ICCBased`: il profilo incorporato; se è illeggibile o non corrisponde a `/N`, `/Alternate`, poi lo
  spazio del dispositivo di `/N` componenti;
- `CalRGB` e `CalGray`: un profilo costruito dai loro parametri (punto di bianco, gamma, matrice);
- **DeviceCMYK**: l'**intento di output** del documento (`/OutputIntents`, PDF/X e PDF/A) se ne ha
  uno CMYK; altrimenti il **profilo di stampa SWOP2006_Coated3v2** (IDEAlliance) dal **pacchetto di
  risorse esterno** (`scripts/fetch_resources.sh`, `icc/SWOP2006_Coated3v2.icc`, 2,7 MB); senza pacchetto
  una **tabella interna** di 9⁴ colori campionata da quel profilo (`scripts/gen_cmyk_table.py`, 20 KB)
  e interpolata in quattro dimensioni.
- Immagini a 8 bit in uno spazio con profilo: convertite **a righe intere** con la trasformazione a 8
  bit ottimizzata di Little CMS.

**Il profilo SWOP.** Licenza IDEAlliance: «may be used, embedded, exchanged, and shared without
restriction. It may not be altered, or sold without written permission». Lo distribuiamo **non
modificato** e solo nel pacchetto esterno, con la sua licenza; nel motore ci sono i colori che dà nei
punti della griglia, non il profilo. I profili ECI (ISO Coated v2) non sono ridistribuibili; quelli
Adobe hanno una licenza propria: scartati.

## Conseguenze

- I colori CMYK e quelli con profilo si avvicinano ad Acrobat (e a pdftoppm): sul corpus, la pagina
  0000861 passa dallo 0,04 allo 0,94 di somiglianza, la 0000744 dallo 0,57 allo 0,89.
- Stesso file, stesso risultato: tabella e profilo danno colori quasi uguali (per la metrica del
  corpus identici), e il documento dichiara la risorsa mancante.
- Una dipendenza C in più sotto fuzzing (`fuzz_icc`); modulo WASM più grande (misurato in PLAN.md).
- Scoperto strada facendo: l'interprete riusava l'ultimo colore registrato quando famiglia e
  componenti coincidevano, così due tinte piatte diverse alla stessa percentuale diventavano una sola;
  ora si confronta anche il colore.
