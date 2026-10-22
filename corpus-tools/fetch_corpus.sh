#!/usr/bin/env bash
# Downloads and extracts the corpus subsets used by run_corpus.py into $CAPRIOLE_CORPUS_DIR
# (the project container's CORPORA/ by default: outside the repository, never versioned).
#
#   corpus-tools/fetch_corpus.sh                         the default subsets (~2.1 GB)
#   corpus-tools/fetch_corpus.sh --safedocs 0417         one zip of SafeDocs (0000-7932)  → safedocs/0417
#   corpus-tools/fetch_corpus.sh --unsafe x022-03        one zip of UNSAFE (corpora-pdf-*) → unsafe/x022-03
#   corpus-tools/fetch_corpus.sh --into DIR ...          into DIR instead of $CAPRIOLE_CORPUS_DIR
#
# Sources (digitalcorpora.org, DARPA SafeDocs program):
#   safedocs/0000  CC-MAIN-2021-31-PDF-UNTRUNCATED zip 0000   1,000 PDFs from the web     1.3 GB
#   unsafe/x002    CC-MAIN-2021-31-UNSAFE corpora-pdf-x002    evalFour                    0.4 GB
#   unsafe/x008    CC-MAIN-2021-31-UNSAFE corpora-pdf-x008    TA3 synthetic malformations 0.4 GB
# The UNSAFE names are not a plain sequence (x000-x021, x022-00…x031-04, x032-x999): nightly.py lists them.
#
# These files are hostile input by design: do not open them with system viewers.
set -euo pipefail

root="${CAPRIOLE_CORPUS_DIR:-$(cd "$(dirname "$0")/../.." && pwd)/CORPORA}"
base=https://digitalcorpora.s3.amazonaws.com/corpora/files
safedocs=() unsafe=()
while (($#)); do
  case "$1" in
    --safedocs) [[ "${2:-}" =~ ^[0-9]{4}$ ]] || { echo "--safedocs takes four digits (0000-7932)" >&2; exit 2; }; safedocs+=("$2"); shift 2 ;;
    --unsafe) [[ "${2:-}" =~ ^x[0-9]{3}(-[0-9]{2})?$ ]] || { echo "--unsafe takes a name like x150 or x022-03" >&2; exit 2; }; unsafe+=("$2"); shift 2 ;;
    --into) [[ -n "${2:-}" ]] || { echo "--into takes a directory" >&2; exit 2; }; root="$2"; shift 2 ;;
    *) echo "usage: $0 [--into DIR] [--safedocs NNNN]... [--unsafe xNNN[-NN]]..." >&2; exit 2 ;;
  esac
done
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

fetch_safedocs() {  # the zips are grouped by thousand: zipfiles/0000-0999/0417.zip
  local n=$((10#$1)) group
  group="$(printf '%04d-%04d' $((n / 1000 * 1000)) $((n / 1000 * 1000 + 999)))"
  fetch "$base/CC-MAIN-2021-31-PDF-UNTRUNCATED/zipfiles/$group/$1.zip" "safedocs-$1.zip" "safedocs/$1"
}

fetch_unsafe() {
  fetch "$base/CC-MAIN-2021-31-UNSAFE/corpora-pdf/corpora-pdf-$1.zip" "unsafe-$1.zip" "unsafe/$1"
}

if ((${#safedocs[@]} + ${#unsafe[@]} == 0)); then
  safedocs=(0000) unsafe=(x002 x008)
fi
for n in "${safedocs[@]}"; do fetch_safedocs "$n"; done
for name in "${unsafe[@]}"; do fetch_unsafe "$name"; done
