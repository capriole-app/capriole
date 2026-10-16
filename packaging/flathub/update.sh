#!/usr/bin/env bash
# Points the manifest at a release: the .deb's URL and its SHA-256 (from the release's SHA256SUMS), and a
# <release> in the metainfo. Run after the release is published, then copy the three files to the Flathub
# repository (README.md).
#
#   packaging/flathub/update.sh 0.1.0 [YYYY-MM-DD]
set -euo pipefail
cd "$(dirname "$0")"
version="${1:?usage: update.sh <version> [date]}"
date="${2:-$(date -u +%F)}"
base="https://github.com/capriole-app/capriole/releases/download/v$version"
sum="$(curl -fsSL "$base/SHA256SUMS" | awk '$2 == "Capriole_amd64.deb" { print $1 }')"
[[ "$sum" =~ ^[0-9a-f]{64}$ ]] || { echo "no Capriole_amd64.deb in $base/SHA256SUMS" >&2; exit 1; }
sed -i -E "s|url: https://github.com/capriole-app/capriole/releases/download/v[^/]+/Capriole_amd64.deb|url: $base/Capriole_amd64.deb|" app.capriole.Capriole.yml
python3 - "$sum" <<'PY'
import re, sys
path = "app.capriole.Capriole.yml"
text = open(path).read()
# The sha256 line right after the .deb's url.
text = re.sub(r"(Capriole_amd64\.deb\n\s+sha256: )[0-9a-f]{64}", r"\g<1>" + sys.argv[1], text)
open(path, "w").write(text)
PY
grep -q "<release version=\"$version\"" app.capriole.Capriole.metainfo.xml ||
  sed -i "s|  <releases>|  <releases>\n    <release version=\"$version\" date=\"$date\"/>|" app.capriole.Capriole.metainfo.xml
grep -n -A1 "Capriole_amd64.deb" app.capriole.Capriole.yml
