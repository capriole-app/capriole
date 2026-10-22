# Image of the nightly corpus job (ci/corpus.Jenkinsfile): the engine's toolchain (cpdf with the corpus
# preset: GCC, optimised, ASan/UBSan) and the reference engines run_corpus.py compares with — qpdf, MuPDF
# (mutool), Poppler (pdfinfo) — as external programs, never linked. Ubuntu 24.04 like ci/Dockerfile and the
# development machine, so a night's numbers compare with a run made by hand.
FROM ubuntu:24.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 unzip util-linux \
      cmake ninja-build g++ \
      qpdf mupdf-tools poppler-utils \
 && rm -rf /var/lib/apt/lists/*
