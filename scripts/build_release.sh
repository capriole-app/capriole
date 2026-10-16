#!/usr/bin/env bash
# A release's files, in dist/release/: the desktop app's installers — for Linux the .deb and the AppImage,
# for Windows the NSIS installer (cross-built with cargo-xwin) — the web app for self-hosting
# (capriole-web.tar.gz: deploy/web/package.sh's context, docs/SELF-HOSTED.md), and SHA256SUMS. The names
# carry no version, so a release's «latest» links stay the same (the version is in the release's tag and
# in the app's «Informazioni»).
#
# Needs the engine built for WebAssembly (build/wasm/bindings/wasm: cmake --preset wasm && cmake --build
# --preset wasm; on Jenkins the CI stage hands it over) and the tools of ci/desktop.Dockerfile:
#
#   docker build -t capriole-desktop -f ci/desktop.Dockerfile ci
#   docker run --rm -u "$(id -u)" -e HOME=/tmp -v "$PWD":/src -w /src capriole-desktop scripts/build_release.sh
#
# The resource pack (CJK fonts, CMaps, the CMYK profile) is fetched and bundled: the desktop works offline.
# Before the installers are kept, the Linux binary is started on a virtual display and must still be running
# after 15 s (it shows the opening screen with the engine loaded; a crash at start fails the build).
set -euo pipefail

cd "$(dirname "$0")/.."
root="$PWD"
out="$root/dist/release"
export CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-4}"  # the machines have little memory to spare

die() { printf '\033[1;31mFAIL: %s\033[0m\n' "$*" >&2; exit 1; }
step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
for tool in cargo cargo-xwin makensis node xvfb-run; do
  command -v "$tool" >/dev/null || die "'$tool' not found — use the image of ci/desktop.Dockerfile"
done
[[ -f build/wasm/bindings/wasm/capriole.wasm ]] || die "the engine is not built for WebAssembly (build/wasm)"

step "resource pack"
scripts/fetch_resources.sh

step "packages of the web app and of the shell"
(cd apps/capriole && npm ci --no-fund --no-audit)
(cd apps/desktop && npm ci --no-fund --no-audit)

cd apps/desktop
step "Linux: .deb and AppImage (builds the web app first)"
npx tauri build --bundles deb,appimage

step "Windows: NSIS installer (cross-built, the same web app)"
npx tauri build --runner cargo-xwin --target x86_64-pc-windows-msvc --bundles nsis \
  --config '{"build":{"beforeBuildCommand":""}}'

step "smoke: the Linux binary on a virtual display"
status=0
timeout 15 xvfb-run -a src-tauri/target/release/capriole-desktop || status=$?
[[ $status == 124 ]] || die "the desktop app stopped by itself (exit $status) within 15 s of starting"
echo "still running after 15 s"

step "dist/release"
version="$(node -p "require('./src-tauri/tauri.conf.json').version")"
bundle=src-tauri/target/release/bundle
rm -rf "$out"
mkdir -p "$out"
cp "$bundle/deb/Capriole_${version}_amd64.deb" "$out/Capriole_amd64.deb"
cp "$bundle/appimage/Capriole_${version}_amd64.AppImage" "$out/Capriole_amd64.AppImage"
cp "src-tauri/target/x86_64-pc-windows-msvc/release/bundle/nsis/Capriole_${version}_x64-setup.exe" "$out/Capriole_x64-setup.exe"
# The web app the shell carries, with its server's files, for self-hosting.
"$root/deploy/web/package.sh"
tar -C "$root/deploy/web" -czf "$out/capriole-web.tar.gz" context
(cd "$out" && sha256sum Capriole_* capriole-web.tar.gz > SHA256SUMS && cat SHA256SUMS)
echo "version $version"
