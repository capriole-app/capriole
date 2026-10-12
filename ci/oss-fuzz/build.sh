#!/bin/bash -eu
# OSS-Fuzz build of Capriole's fuzz targets (fuzz/): run by OSS-Fuzz's builder, with its compiler and flags
# in the environment — CC, CXX, CFLAGS, CXXFLAGS (the sanitizer and the coverage instrumentation),
# LIB_FUZZING_ENGINE (the engine's runtime), SRC, WORK, OUT. The dependencies' archives are already in
# $SRC/capriole-deps (the Dockerfile ran fetch_deps.py): nothing is downloaded here.
#
# Each target goes to $OUT with its seed corpus (fuzz/corpus/<name>) as <target>_seed_corpus.zip.

cmake -S "$SRC/capriole" -B "$WORK/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCAPRIOLE_BUILD_TESTS=OFF \
  -DCAPRIOLE_BUILD_FUZZERS=ON \
  -DCAPRIOLE_FUZZ_INSTRUMENT=OFF \
  -DCAPRIOLE_FUZZING_ENGINE="$LIB_FUZZING_ENGINE" \
  -DCAPRIOLE_DEPS_DIR="$SRC/capriole-deps"
cmake --build "$WORK/build" -j"${CAPRIOLE_JOBS:-$(nproc)}"

for target in "$WORK"/build/fuzz/fuzz_*; do
  [[ -f "$target" && -x "$target" ]] || continue
  name=$(basename "$target")
  cp "$target" "$OUT/"
  corpus="$SRC/capriole/fuzz/corpus/${name#fuzz_}"
  if [[ -d "$corpus" ]]; then
    (cd "$corpus" && zip -q -r -X "$OUT/${name}_seed_corpus.zip" .)
  fi
done
