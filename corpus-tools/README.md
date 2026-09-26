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

Isolation: separate process per tool, empty working directory, timeout, 4 GB memory cap (prlimit;
ASan's own RSS limit for Capriole). No network namespace (not available unprivileged here) — the tools
do not use the network, and corpus files must never be opened with anything else.

Requirements: python3, qpdf, mupdf-tools, poppler-utils, prlimit (util-linux), unzip, curl.
