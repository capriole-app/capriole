#!/usr/bin/env bash
# The resource pack (ADR 0005): data too large to build into the engine, which a host supplies when it
# has it (engine/include/capriole/font/resources.hpp). Downloads pinned versions, checks their SHA-256,
# and lays them out as the engine names them:
#
#   <dir>/cmap/<name>          Adobe's predefined CMaps (BSD-3-Clause), for Chinese, Japanese, Korean
#   <dir>/font/Liberation*.ttf  Liberation 2.1.5 (OFL 1.1): glyphs the built-in substitutes lack
#   <dir>/font/NotoSans??-Regular.otf  Noto Sans CJK 2.004 (OFL 1.1): substitutes for CJK fonts that
#                              are not embedded (JP, SC, TC, KR; 4.5 to 8.3 MB each)
#   <dir>/LICENSES/            their licences
#
#   scripts/fetch_resources.sh [dir]     default: build/resources
#
# Then: cpdf render --resources build/resources ...; the Inspector serves the same directory.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT="${1:-build/resources}"
CACHE="$OUT/.downloads"
mkdir -p "$CACHE" "$OUT/cmap" "$OUT/font" "$OUT/LICENSES"

fetch() {  # url sha256 file
  local url=$1 sum=$2 file="$CACHE/$3"
  if [[ ! -f "$file" ]] || ! echo "$sum  $file" | sha256sum -c --quiet 2>/dev/null; then
    curl -fsSL --retry 3 -o "$file.part" "$url"
    mv "$file.part" "$file"
  fi
  echo "$sum  $file" | sha256sum -c --quiet || { echo "SHA-256 mismatch: $url" >&2; rm -f "$file"; exit 1; }
}

# Adobe CMap resources, tag 20231115: the CMaps of the CJK collections (not the deprecated ones).
fetch https://github.com/adobe-type-tools/cmap-resources/archive/refs/tags/20231115.tar.gz \
  a88c3beee8d2f139b47f9d7e932ec198be24991dd46fcaef2961b4b5ea855ea2 cmap-resources-20231115.tar.gz
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar xzf "$CACHE/cmap-resources-20231115.tar.gz" -C "$tmp"
for collection in Adobe-Japan1-7 Adobe-GB1-6 Adobe-CNS1-7 Adobe-Korea1-2 Adobe-KR-9; do
  cp "$tmp"/cmap-resources-20231115/"$collection"/CMap/* "$OUT/cmap/"
done
cp "$tmp/cmap-resources-20231115/LICENSE.md" "$OUT/LICENSES/cmap-resources.md"

# Liberation 2.1.5.
fetch https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz \
  7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0 liberation-fonts-ttf-2.1.5.tar.gz
tar xzf "$CACHE/liberation-fonts-ttf-2.1.5.tar.gz" -C "$tmp"
cp "$tmp"/liberation-fonts-ttf-2.1.5/*.ttf "$OUT/font/"
cp "$tmp/liberation-fonts-ttf-2.1.5/LICENSE" "$OUT/LICENSES/liberation.txt"

# Noto Sans CJK 2.004, the region subsets.
noto=https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004
fetch "$noto/Sans/SubsetOTF/JP/NotoSansJP-Regular.otf" dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073 NotoSansJP-Regular.otf
fetch "$noto/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf" faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9 NotoSansSC-Regular.otf
fetch "$noto/Sans/SubsetOTF/TC/NotoSansTC-Regular.otf" 5bab0cb3c1cf89dde07c4a95a4054b195afbcfe784d69d75c340780712237537 NotoSansTC-Regular.otf
fetch "$noto/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf" 69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68 NotoSansKR-Regular.otf
fetch "$noto/LICENSE" 6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2 noto-cjk-LICENSE
cp "$CACHE"/NotoSans??-Regular.otf "$OUT/font/"
cp "$CACHE/noto-cjk-LICENSE" "$OUT/LICENSES/noto-cjk.txt"

echo "resource pack in $OUT: $(find "$OUT/cmap" -type f | wc -l) CMaps, $(find "$OUT/font" -type f | wc -l) fonts, $(du -sh --exclude=.downloads "$OUT" | cut -f1)"
