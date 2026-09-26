#!/usr/bin/env bash
# The gate. Green here = ready to commit/push. Same steps as CI (.github/workflows/ci.yml).
#
#   scripts/check.sh            format + build/test with ASan/UBSan + clang-tidy
#   scripts/check.sh --fix      apply clang-format instead of checking it
#   SKIP_TIDY=1 / SKIP_WASM=0   opt out of tidy / opt in to the WASM build+tests (needs EMSDK)
#
# A step that cannot run because a tool is missing FAILS the gate; it is never silently skipped.
set -euo pipefail

cd "$(dirname "$0")/.."

FIX=0
[[ "${1:-}" == "--fix" ]] && FIX=1
SKIP_TIDY="${SKIP_TIDY:-0}"
SKIP_WASM="${SKIP_WASM:-1}"

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mFAIL: %s\033[0m\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "'$1' not found — see docs/SETUP.md"; }

mapfile -t SOURCES < <(git ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp' '*.h' | grep -v '^build/')

step "clang-format"
need clang-format
if (( FIX )); then
  clang-format -i "${SOURCES[@]}"
else
  clang-format --dry-run --Werror "${SOURCES[@]}"
fi

step "build + test (asan preset: ASan + UBSan, warnings as errors)"
need cmake
need ninja
cmake --preset asan >/dev/null
cmake --build --preset asan
ctest --preset asan

if [[ "$SKIP_TIDY" != 1 ]]; then
  step "clang-tidy"
  # Needs clang-tidy >= 19: Clang 18 cannot see std::expected in libstdc++ 13 (it gates it on
  # __cpp_concepts >= 202002, which Clang reports only from 19). Ubuntu ships versioned binaries.
  if [[ -z "${CLANG_TIDY:-}" ]]; then
    for candidate in clang-tidy clang-tidy-20 clang-tidy-19; do
      command -v "$candidate" >/dev/null || continue
      major=$("$candidate" --version | grep -oE 'version [0-9]+' | grep -oE '[0-9]+')
      if (( major >= 19 )); then CLANG_TIDY=$candidate; break; fi
    done
  fi
  [[ -n "${CLANG_TIDY:-}" ]] || die "clang-tidy >= 19 not found — see docs/SETUP.md"
  mapfile -t TIDY_SOURCES < <(printf '%s\n' "${SOURCES[@]}" | grep -E '^(engine|capi|tools)/.*\.cpp$')
  # The compile database comes from the GCC build: ignore GCC-only warning flags.
  "$CLANG_TIDY" -p build/asan --quiet --extra-arg=-Wno-unknown-warning-option "${TIDY_SOURCES[@]}"
else
  printf '\033[1;33m(clang-tidy skipped by SKIP_TIDY=1)\033[0m\n'
fi

if [[ "$SKIP_WASM" != 1 ]]; then
  step "WebAssembly build + tests"
  [[ -n "${EMSDK:-}" ]] || die "EMSDK not set — run: source ~/emsdk/emsdk_env.sh"
  cmake --preset wasm >/dev/null
  cmake --build --preset wasm
  ctest --preset wasm
fi

printf '\n\033[1;32mGate green.\033[0m\n'
