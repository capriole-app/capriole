# corpus-tools

Differential testing on real and hostile PDFs (DOCS/CAPRIOLE.md §7.2–7.3, PLAN.md phase 4).

```sh
corpus-tools/fetch_corpus.sh                                  # ~2.1 GB into ../CORPORA (outside the repo)
cmake --preset asan && cmake --build --preset asan            # Capriole under ASan/UBSan
corpus-tools/run_corpus.py ../CORPORA/safedocs/0000 --name safedocs-0000 --report-dir ../CORPORA/reports
corpus-tools/run_corpus.py ../CORPORA/unsafe/x008   --name unsafe-x008   --report-dir ../CORPORA/reports
```

`run_corpus.py` runs `cpdf check` and three reference engines — qpdf, MuPDF (`mutool info`),
Poppler (`pdfinfo`) — as **external programs** (never linked: licences, §7.3). The expected page count
is the one at least two references agree on. Each run writes `results.jsonl` and `summary.md` and, from
the second run of the same `--name` on, lists regressions against the previous run.

`--roundtrip` also saves every readable file twice and checks the outputs (PLAN.md phase 3):
- full save (`cpdf save`): `qpdf --check` on the output, strict re-read by Capriole, same page count;
- incremental save with a new `/ModDate` (`cpdf save --incremental`): the original bytes come first,
  unchanged; qpdf is no worse than on the original; strict re-read (compared with the original's own);
  same page count. Refusals (repaired or encrypted files) are counted by reason.

Run it at low intensity on a workstation (`nice -n 19`, `--jobs 2`), and don't rebuild `build/asan`
while it runs.

Isolation: separate process per tool, empty working directory, timeout, 4 GB memory cap (prlimit;
ASan's own RSS limit for Capriole). No network namespace (not available unprivileged here) — the tools
do not use the network, and corpus files must never be opened with anything else.

Requirements: python3, qpdf, mupdf-tools, poppler-utils, prlimit (util-linux), unzip, curl.

## Pixel comparison of the rendering (PLAN.md phase 6, exit criterion)

```sh
cmake --preset release && cmake --build --preset release     # cpdf, optimised
scripts/fetch_resources.sh                                     # the resource pack (CMaps, CJK fonts, SWOP)
nice -n 10 corpus-tools/compare_render.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \
    --pages 1000 --jobs 3 --report-dir ../CORPORA/reports --name render-safedocs-0000
```

One page per file, chosen by a hash of `--seed` and the file name (the same sample at every run, not only
first pages), drawn by Capriole, `pdftoppm` and `mutool draw` at 72 dpi from the CropBox. Similarity: the
share of pixels differing by at most 32/255 per channel after a 1-pixel blur (anti-aliasing is not a
difference). A page **passes** when it is at least 0.95 similar to one reference (with two references,
the majority is one agreeing reference); **strict** when close to both. The summary also gives how
often the two references agree with each other — the ceiling of what "close to both" can reach —,
failures by category (from Capriole's warnings), the 30 worst pages side by side in `worst/`, and
regressions against the previous run of the same `--name`. Exit criterion of phase 6: pass rate ≥ 95%.

## Text comparison (PLAN.md phase 7, exit criterion)

```sh
cmake --preset release && cmake --build --preset release     # cpdf, optimised
nice -n 10 corpus-tools/compare_text.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \
    --pages 1000 --jobs 3 --report-dir ../CORPORA/reports --name text-safedocs-0000
```

One page per file, chosen by a hash of `--seed`, the file name and the page number; when the files are
fewer than `--pages`, further rounds (`--rounds`, default 3) take another page from the files that have
one. Each page is extracted by Capriole (`cpdf text`), `pdftotext` and `mutool draw -F txt`; the texts are
normalized (NFKC, soft hyphens dropped) and split into words at white space. **Content**: the F1 of the
words as multisets; **order**: the similarity of the word sequences (difflib's ratio). A page **passes**
when, against the same reference, content ≥ 0.98 and order ≥ 0.90; **strict** when against both.
Pages where no reference finds a word are left out. The summary gives the rates, how often the two
references agree with each other, failures by kind (content, order, nothing extracted), the 30 worst
pages with their three texts in `worst/`, and regressions against the previous run of the same `--name`.
`--reading-order layout` measures Capriole's column order instead of the content's. Exit criterion of
phase 7: pass rate ≥ 95%.

## Page operations (PLAN.md phase 8a, exit criterion)

```sh
nice -n 10 corpus-tools/check_pages.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \
    --files 200 --jobs 3 --report-dir ../CORPORA/reports --name pages-safedocs-0000
```

For each file (hash order of `--seed`), saved fully first, `cpdf` rotates a page (plainly and
incrementally), arranges three pages, extracts one and merges it with the whole file. Each output must
pass `qpdf --check` no worse than the original, and its pages must match the pages they come from as
pdftoppm draws them (similarity ≥ 0.99 with the metric of compare_render.py); a rotated page is checked
by its rotation (`pdfinfo`) and by rotating it back, which must give the original pixels. Exit criterion
of phase 8a: every file passes.

## Annotations (PLAN.md phase 8d, exit criterion)

```sh
nice -n 10 corpus-tools/check_annotations.py ../CORPORA/safedocs/0000 --cpdf build/release/tools/cpdf/cpdf \
    --files 100 --jobs 3 --report-dir ../CORPORA/reports --name annotations-safedocs-0000
```

Two checks, every annotation made by `cpdf annot add` (the C API): **fidelity** — on a blank page,
upright and turned by /Rotate 90, each of the ten kinds drawn by pdftoppm and by mutool as by Capriole
(similarity ≥ 0.99, the metric of compare_render.py) —; **real files** — on one page per file the ten
kinds are added (text markup over horizontal words of the page), saved incrementally; `qpdf --check`
no worse than the original, the annotations read back, and each drawn by all three engines inside its
rectangle (≥ 0.3% of its pixels changed). Exit criterion of phase 8d (engine): everything passes.

## Forms (PLAN.md phase 8e, exit criterion)

```sh
nice -n 10 corpus-tools/check_forms.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \
    --cpdf build/release/tools/cpdf/cpdf --jobs 2 --report-dir ../CORPORA/reports --name forms-all
```

Every fillable field of every form — the corpus files whose form has fields, and two forms made by the
script with a field of every kind (upright, and on a page turned by /Rotate 90 with its widgets turned
back by /MK /R) — gets a value different from its own (`cpdf form fill`, the C API, saved
incrementally). The output must read back in MuPDF (`mutool run`: `getValue()`, or `/V` itself for
several values) and in Capriole with exactly the values written; every engine (Capriole, Poppler,
MuPDF) must show the change in each visible widget (≥ 1% of its pixels; a button's widgets only where
they turn on or off); `qpdf --check` no worse than the original. Forms with nothing to fill (signatures,
push buttons, read-only fields) and files that do not open are reported apart. Exit criterion of phase
8e (engine): every field of every form passes.

## Attachments and Factur-X (PLAN.md phase 8f, exit criterion)

```sh
nice -n 10 corpus-tools/check_attachments.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \
    --cpdf build/release/tools/cpdf/cpdf --jobs 3 --report-dir ../CORPORA/reports --name attachments-all \
    [--facturx-python ../CORPORA/tools/venv/bin/python] [--verapdf ../CORPORA/tools/verapdf/verapdf]
```

Three checks. **Reading**: every file whose attachments `pdfdetach` lists — Capriole lists the same
names, and extracts each byte for byte as `pdfdetach -saveall`. **Adding and removing**: on 100 files a
file with a name beyond ASCII is attached (incremental save); Poppler, qpdf (`--list-attachments`,
`--show-attachment`) and MuPDF (the name tree walked by `mutool run`) list it and give its bytes back,
`qpdf --check` is no worse; removed, none lists it. **Factur-X**: an invoice of each profile embedded in
a blank page and in 20 files; Poppler extracts `factur-x.xml` identical; MuPDF finds it in `/AF` with
the relationship of its profile, `text/xml` and `/ModDate`; the XMP (lxml) is well-formed with the fx
properties and one extension schema, PDF/A-3 declared only if it was. With `--facturx-python` (a
Python with the `factur-x` package) the reference library reads each invoice back; with `--verapdf`,
veraPDF validates as PDF/A-3B the outputs whose input was PDF/A-3. Exit criterion of phase 8f
(engine): every check passes.

## Redaction (PLAN.md phase 8g, exit criterion)

```sh
nice -n 10 corpus-tools/check_redaction.py ../CORPORA/safedocs/0000 ../CORPORA/unsafe/x002 ../CORPORA/unsafe/x008 \
    --cpdf build/release/tools/cpdf/cpdf --jobs 3 --report-dir ../CORPORA/reports --name redaction-all
```

On 100 pages (one per file with text), three words of the page are redacted (their boxes from
`cpdf text --words`, filled black) and, when the page draws an image of at least 40 × 40 points
(`pdftohtml -xml`), its left half. Then: no text with its centre in an area for Capriole, Poppler
(`pdftotext -bbox`) and MuPDF (`-F stext`); the text clear of the areas as before for all three (pieces
of words cut at an area's edge excepted); the page alone (`qpdf --pages`), decompressed, has each word
it showed once — when written as it reads — at least once fewer (whole words: not in names like
`/question`); the image, as Poppler extracts it, has a half of one colour it did not have; the page
outside the areas draws as before in all three engines (similarity ≥ 0.99, areas whitened); `qpdf --check`
no worse. Exit criterion of phase 8g (engine): every page passes.

## Firme digitali

`check_signatures.py` (criterio di uscita della fase 8h, motore): con la PKI di prova di
`tests/data/signing` firma `--files` file dei corpora (RSA ed ECDSA, visibili e invisibili, uno su
quattro con marca temporale di una TSA locale, `openssl ts -reply`) e controlla che Poppler (`pdfsig`,
NSS con la CA di prova) e OpenSSL (`openssl cms -verify`, `openssl ts -verify`) li trovino validi, che
la firma sia una revisione incrementale e `qpdf --check` non peggiori, che Capriole la rilegga valida e
fidata al livello fatto, e che un byte cambiato la renda non valida per tutti e tre; su ogni file dei
corpora con una `/ByteRange` confronta il verdetto di Capriole con quello di Poppler. Strumenti esterni:
`pdfsig`, `certutil` (NSS), `openssl`, `qpdf`.

## PDF/A

`check_pdfa.py` (criterio di uscita della fase 8i, motore): converte `--files` file dei corpora con
`cpdf pdfa` e controlla che veraPDF (`--verapdf`) dia conformi i file convertiti (2b o 3b), che Capriole
li disegni come l'originale riscritto per intero, che Poppler non perda caratteri, che `qpdf --check` non
peggiori; conta i rifiuti per motivo; sui primi `--invoices` incorpora una fattura Factur-X e converte in
PDF/A-3 (veraPDF 3b e, con `--facturx-python`, la libreria factur-x). veraPDF gira senza il limite di
memoria di `run()` (la JVM riserva molto più spazio di quanto usa) e con l'heap limitato.
