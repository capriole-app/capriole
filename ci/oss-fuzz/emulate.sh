#!/usr/bin/env bash
# Runs build.sh as OSS-Fuzz would (its flags for libFuzzer + a sanitizer), locally and without network,
# then each target for a few seconds on its seed corpus. A check before submitting the project, not
# OSS-Fuzz itself (that is infra/helper.py in the oss-fuzz repository, with Docker).
#
#   ci/oss-fuzz/emulate.sh <deps dir> [address|undefined] [seconds per target]
#
# The deps dir holds the archives (ci/oss-fuzz/fetch_deps.py). CAPRIOLE_JOBS limits the build's jobs.
# The work directory (a temporary one) is removed when every target passed, kept otherwise.
set -euo pipefail
deps=$(realpath "$1")
sanitizer=${2:-address}
seconds=${3:-20}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
mkdir -p "$work/src" "$work/out" "$work/work"
ln -s "$root" "$work/src/capriole"
ln -s "$deps" "$work/src/capriole-deps"

flags="-O1 -fno-omit-frame-pointer -gline-tables-only -fsanitize=fuzzer-no-link"
case "$sanitizer" in
  address) flags+=" -fsanitize=address -fsanitize-address-use-after-scope" ;;
  undefined) flags+=" -fsanitize=array-bounds,bool,builtin,enum,function,integer-divide-by-zero,null,object-size,return,returns-nonnull-attribute,shift,signed-integer-overflow,unsigned-integer-overflow,unreachable,vla-bound,vptr -fno-sanitize-recover=array-bounds,bool,builtin,enum,function,integer-divide-by-zero,null,object-size,return,returns-nonnull-attribute,shift,signed-integer-overflow,unreachable,vla-bound,vptr" ;;
  *) echo "sanitizer: address or undefined" >&2; exit 2 ;;
esac

CC=${CC:-clang-20} CXX=${CXX:-clang++-20} CFLAGS="$flags" CXXFLAGS="$flags" LIB_FUZZING_ENGINE=-fsanitize=fuzzer \
  SRC="$work/src" WORK="$work/work" OUT="$work/out" SANITIZER="$sanitizer" bash "$root/ci/oss-fuzz/build.sh"

failed=0
count=0
for target in "$work"/out/fuzz_*; do
  [[ -x "$target" && ! "$target" == *.zip ]] || continue
  name=$(basename "$target")
  count=$((count + 1))
  corpus="$work/corpus-$name"
  mkdir -p "$corpus"
  [[ -f "$target"_seed_corpus.zip ]] && unzip -q -o "$target"_seed_corpus.zip -d "$corpus"
  if ! (cd "$work" && "$target" -max_total_time="$seconds" -rss_limit_mb=2560 -timeout=25 "$corpus" >"$work/$name.log" 2>&1); then
    echo "FAIL $name (log: $work/$name.log)"
    failed=$((failed + 1))
  else
    echo "ok   $name $(grep -oE 'Done [0-9]+ runs' "$work/$name.log" | head -1)"
  fi
done
if [[ $count -gt 0 && $failed -eq 0 ]]; then
  echo "$count targets, none failed"
  rm -rf "$work"
else
  echo "$count targets, $failed failed (logs and build kept in $work)"
  exit 1
fi
