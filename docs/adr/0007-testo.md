# ADR 0007 — Estrazione del testo: ordine del contenuto, provenienza per carattere

Data: 2026-09-30 · Stato: **accettata**

## Contesto

La fase 7 ricostruisce dal flusso di glifi di una pagina il testo in ordine di lettura, con la
provenienza di ogni carattere (base del modello alto, della ricerca, della selezione e della
redaction). Due scuole: **riordinare per geometria** (pdftotext, pdfminer: blocchi, colonne, flussi) o
**seguire l'ordine del contenuto** unendo ciò che la geometria dice contiguo (MuPDF). Il criterio di
uscita, fissato con l'utente nella forma più severa, chiede che su ≥ 95% di 1.000 pagine il testo
concordi con almeno uno dei due riferimenti sia nel contenuto (F1 delle parole ≥ 0,98) sia
nell'ordine (somiglianza delle sequenze ≥ 0,90).

## Decisione

- **Ordine del contenuto per default** (`text::Order::kContent`): le parole si formano dalle distanze
  lungo la linea di base (0,15 em dalla fine della larghezza del glifo, senza `Tc`, come MuPDF); le
  righe e i blocchi seguono il contenuto e si uniscono dove la geometria continua (la riga a destra
  sulla stessa linea di base — le righe delle tabelle —, la riga appena sotto e sovrapposta). Misurato
  sul campione: 95,3% contro il 70% del riordino a colonne (regole di Breuel, `Order::kLayout`, che
  resta come opzione). Un ibrido — riordinare solo dove il contenuto risale spesso dentro una colonna —
  correggeva una pagina ogni 34 che rompeva: scartato.
- **Il testo dei glifi è calcolato dall'interprete** quando lo si chiede (`Purpose::kText`): lì c'è il
  font (ToUnicode, nomi, collezioni, cmap del programma). Il disegno non paga il costo né chiede le
  risorse che servono solo al testo (la CMap Unicode di una collezione CJK).
- **Doppioni tolti**: lo stesso glifo o la stessa parola ridisegnati quasi nello stesso punto (grassetto
  simulato, ombre) si leggono una volta, come pdftotext.
- **Provenienza per carattere**: stream (pagina o form XObject / procedura di glifo), offset e ordinale
  dell'operatore, byte del codice, font; il riquadro nelle coordinate della pagina come la si vede.
- **U+FFFD** per un glifo di cui il font non dice nulla (come MuPDF): il testo c'è ma è illeggibile,
  e chi cerca o seleziona lo vede.

## Conseguenze

- Il testo dipende dalla qualità dell'ordine con cui il file è stato scritto: nei moduli disegnati fuori
  ordine pdftotext legge meglio. Il modello alto potrà usare la struttura (tag, §14.7) dove c'è.
- I due riferimenti concordano fra loro solo su metà delle pagine: la misura premia chi segue una delle
  due scuole con coerenza, non la media.
- Le scritture complesse (devanagari, arabo in ordine visivo) escono nell'ordine dei glifi; il
  riordino bidirezionale è lavoro futuro.
