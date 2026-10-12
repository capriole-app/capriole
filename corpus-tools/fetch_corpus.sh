#!/usr/bin/env bash
# Downloads and extracts the corpus subsets used by run_corpus.py into $CAPRIOLE_CORPUS_DIR
# (the project container's CORPORA/ by default: outside the repository, never versioned).
#
#   corpus-tools/fetch_corpus.sh            the default subsets (~2.1 GB)
#
# Sources (digitalcorpora.org, DARPA SafeDocs program):
#   safedocs/0000  CC-MAIN-2021-31-PDF-UNTRUNCATED zip 0000   1,000 PDFs from the web     1.3 GB
#   unsafe/x002    CC-MAIN-2021-31-UNSAFE corpora-pdf-x002    evalFour                    0.4 GB
#   unsafe/x008    CC-MAIN-2021-31-UNSAFE corpora-pdf-x008    TA3 synthetic malformations 0.4 GB
#
# These files are hostile input by design: do not open them with system viewers.
set -euo pipefail

root="${CAPRIOLE_CORPUS_DIR:-$(cd "$(dirname "$0")/../.." && pwd)/CORPORA}"
base=https://digitalcorpora.s3.amazonaws.com/corpora/files
mkdir -p "$root/downloads"

fetch() {  # fetch <url> <zip name> <target dir>
  local url=$1 zip="$root/downloads/$2" target="$root/$3"
  if [[ -d "$target" && -n "$(ls -A "$target")" ]]; then
    echo "$3: already extracted"
    return
  fi
  echo "$3: downloading $url"
  # Resume on failure, and give up on a connection that stalls (it happened: a transfer hung for
  # minutes without closing). One curl at a time per file: two writers corrupt the archive.
  local attempt
  for attempt in $(seq 1 30); do
    curl -sS --fail -C - --speed-limit 5000 --speed-time 60 -o "$zip" "$url" && break
    echo "$3: retrying ($attempt)"
    sleep 10
  done
  unzip -tq "$zip" >/dev/null || { echo "$3: archive is damaged, delete $zip and retry" >&2; return 1; }
  mkdir -p "$target"
  # -j: flatten the archive's folders; -n: never overwrite; unzip refuses absolute and ../ paths.
  unzip -q -j -n "$zip" -d "$target"
  echo "$3: $(find "$target" -type f | wc -l) files"
}

fetch "$base/CC-MAIN-2021-31-PDF-UNTRUNCATED/zipfiles/0000-0999/0000.zip" safedocs-0000.zip safedocs/0000
fetch "$base/CC-MAIN-2021-31-UNSAFE/corpora-pdf/corpora-pdf-x002.zip" unsafe-x002.zip unsafe/x002
fetch "$base/CC-MAIN-2021-31-UNSAFE/corpora-pdf/corpora-pdf-x008.zip" unsafe-x008.zip unsafe/x008
