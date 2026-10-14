#!/usr/bin/env bash
# Before the repository is made public (docs/PUBBLICAZIONE.md): what it would show must be meant to be shown.
#
#   scripts/prepublish_check.sh [--allow-pending] [--build-dir build/asan]
#
# Checks, over the whole history (every commit, not only the files as they are now):
#   1. no secrets: private keys outside the test PKI, cloud and API tokens, passwords in URLs;
#   2. no private paths (a home directory, Dropbox), no e-mail address but the project's (commits' metadata too);
#   3. no file over 5 MB;
# and in the tree as it is:
#   4. the licence and the public project files are there, and no GitHub Actions workflow (CI is Jenkins);
#   5. THIRD_PARTY_NOTICES.md / third_party/notices.json and the desktop's crate notices are up to date;
#   6. no placeholder left (<<CONTACT-TBD>>: decisions still to take). --allow-pending reports them
#      without failing — the state while publication is being prepared, not when it happens.
set -uo pipefail
cd "$(dirname "$0")/.."

allow_pending=0
build_dir=build/asan
while [[ $# -gt 0 ]]; do
  case "$1" in
    --allow-pending) allow_pending=1 ;;
    --build-dir) build_dir=$2; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

failures=0
pending=0
ok() { printf '\033[1;32mok\033[0m      %s\n' "$1"; }
fail() { printf '\033[1;31mFAIL\033[0m    %s\n' "$1"; failures=$((failures + 1)); }
todo() { printf '\033[1;33mpending\033[0m %s\n' "$1"; pending=$((pending + 1)); }

revisions=$(git rev-list --all)
# The test PKI (tests/data/signing/README.md) and the fuzzers' seeds hold keys on purpose.
exclude=(':!tests/data/signing' ':!fuzz/corpus' ':!tests/regression/pkcs12')

# 1. Secrets, in every commit.
secrets='-----BEGIN ([A-Z]+ )?PRIVATE KEY-----|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{40,}|xox[baprs]-[A-Za-z0-9-]{10,}|AIza[0-9A-Za-z_-]{35}|sk-[A-Za-z0-9]{32,}|[a-z]+://[^/[:space:]:@]+:[^/[:space:]@]{3,}@'
# shellcheck disable=SC2086
hits=$(git grep -I -l -E "$secrets" $revisions -- . "${exclude[@]}" 2>/dev/null | sed 's/^[0-9a-f]*://' | sort -u)
if [[ -z "$hits" ]]; then ok "no secrets in the history"; else fail "possible secrets in: $(echo $hits)"; fi

# 2. Private paths, in every commit.
# shellcheck disable=SC2086
# (A Dropbox folder, not the name: "Copyright (c) 2016 Dropbox, Inc." is in licences.)
# (/home/node/ is the official Node image's home, inside the reports container: not a person's.)
hits=$(git grep -I -h -o -E '/home/[a-z]+/|/Dropbox( \([^)]*\))?/' $revisions -- . ':!scripts/prepublish_check.sh' 2>/dev/null | grep -v '^/home/node/' | sort -u)
if [[ -z "$hits" ]]; then ok "no private paths in the history"; else fail "private paths in the history: $(echo $hits)"; fi

# 2b. E-mail addresses, in every commit and in the commits' metadata: only the project's own
# (@capriole.app), GitHub's noreply ones and examples. Third parties' addresses in licence notices are theirs.
email='[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}'
allowed='@capriole\.app$|@users\.noreply\.github\.com$|^noreply@(github|anthropic)\.com$|@example\.(com|org|net)$|^git@github\.com$|@[0-9]+x\.png$'
# shellcheck disable=SC2086
hits=$( (git grep -I -h -o -E "$email" $revisions -- . ':!THIRD_PARTY_NOTICES.md' ':!third_party/notices.json' \
          ':!apps/desktop/crate-notices.json' 2>/dev/null
         git log --all --format='%ae%n%ce%n%B' | grep -o -E "$email") | sort -u | grep -v -E "$allowed")
if [[ -z "$hits" ]]; then ok "no personal e-mail address in the history"; else fail "e-mail addresses in the history: $(echo $hits)"; fi

# 3. Large files, in every commit.
big=$(git rev-list --objects --all | git cat-file --batch-check='%(objecttype) %(objectsize) %(rest)' | awk '$1 == "blob" && $2 > 5242880 {print $3 " (" $2 " bytes)"}')
if [[ -z "$big" ]]; then ok "no file over 5 MB in the history"; else fail "large files: $big"; fi

# 4. The public project files.
missing=()
for f in LICENSE README.md CONTRIBUTING.md SECURITY.md CODE_OF_CONDUCT.md THIRD_PARTY_NOTICES.md docs/PUBBLICAZIONE.md; do
  git ls-files --error-unmatch "$f" >/dev/null 2>&1 || missing+=("$f")
done
if [[ ${#missing[@]} -eq 0 ]]; then ok "licence and project files committed"; else fail "not committed: ${missing[*]}"; fi
if git ls-files .github/workflows | grep -q .; then fail "GitHub Actions workflows present (CI is Jenkins)"; else ok "no GitHub Actions workflow"; fi

# 5. Notices up to date.
if python3 scripts/gen_notices.py --check --build-dir "$build_dir" >/dev/null 2>&1; then
  ok "engine notices up to date"
else
  fail "engine notices: $(python3 scripts/gen_notices.py --check --build-dir "$build_dir" 2>&1 | tail -1)"
fi
if command -v cargo >/dev/null 2>&1; then
  if python3 scripts/gen_crate_notices.py --check >/dev/null 2>&1; then ok "desktop crate notices up to date"; else fail "desktop crate notices out of date (scripts/gen_crate_notices.py)"; fi
else
  fail "cargo not found: the desktop crate notices cannot be checked"
fi

# 6. Placeholders: decisions still to take.
placeholders=$(git grep -l '<<CONTACT-TBD>>' -- . ':!scripts/prepublish_check.sh' ':!docs' 2>/dev/null)
if [[ -z "$placeholders" ]]; then
  ok "no placeholder left"
elif [[ $allow_pending -eq 1 ]]; then
  todo "contact to decide (<<CONTACT-TBD>>) in: $(echo $placeholders)"
else
  fail "placeholder <<CONTACT-TBD>> still in: $(echo $placeholders)"
fi

echo
if [[ $failures -gt 0 ]]; then
  echo "$failures check(s) failed."
  exit 1
fi
if [[ $pending -gt 0 ]]; then
  echo "Ready but for $pending decision(s) to take (see docs/PUBBLICAZIONE.md)."
else
  echo "Ready to publish."
fi
