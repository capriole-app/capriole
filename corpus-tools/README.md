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
