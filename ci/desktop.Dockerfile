# Image for the desktop app's installers (scripts/build_release.sh; Jenkinsfile, stage «Release (tag)»): the Tauri
# shell for Linux (.deb, AppImage) and, cross-built, for Windows (NSIS setup.exe). The engine is not built
# here: in the desktop it is the WebAssembly module, which the CI stage builds and hands over (a stash).
#
# Windows: cargo-xwin compiles and links the Rust crate for x86_64-pc-windows-msvc with clang-cl and
# lld-link, against Microsoft's CRT and Windows SDK, which it downloads (xwin) into the image at build time.
# Using them means accepting Microsoft's licence for those files (the Visual Studio build tools' one: used
# only to build software, not redistributed). NSIS (makensis) makes the installer.
#
#   docker build -t capriole-desktop -f ci/desktop.Dockerfile ci
FROM ubuntu:24.04

ARG RUST_VERSION=1.99.0
ARG CARGO_XWIN_VERSION=0.23.1
ARG NODE_VERSION=22.23.3

# WebKitGTK and the rest of docs/SETUP.md's list; file and the AppImage's tools for the bundler; clang,
# lld and llvm (llvm-rc for the Windows resources) for cargo-xwin; nsis; xvfb for the smoke run.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 xz-utils file patchelf \
      build-essential pkg-config libwebkit2gtk-4.1-dev libxdo-dev libssl-dev \
      libayatana-appindicator3-dev librsvg2-dev \
      clang-20 lld-20 llvm-20 nsis \
      xvfb xauth \
 && rm -rf /var/lib/apt/lists/*

ENV PATH=/usr/lib/llvm-20/bin:/opt/cargo/bin:$PATH \
    RUSTUP_HOME=/opt/rustup \
    CARGO_HOME=/opt/cargo \
    XWIN_CACHE_DIR=/opt/xwin \
    # linuxdeploy (the AppImage) runs from its own AppImage, which needs FUSE: extract and run instead.
    APPIMAGE_EXTRACT_AND_RUN=1

# Rust, pinned, with the Windows target; cargo-xwin, pinned; the CRT and the SDK fetched once, here.
# World-writable because Jenkins runs the container as its own uid (cargo writes its registry cache).
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
      | sh -s -- -y --no-modify-path --profile minimal --default-toolchain "${RUST_VERSION}" \
 && rustup target add x86_64-pc-windows-msvc \
 && cargo install --locked cargo-xwin --version "${CARGO_XWIN_VERSION}" \
 && cargo xwin env --target x86_64-pc-windows-msvc > /dev/null \
 && chmod -R a+rwX /opt/cargo /opt/rustup /opt/xwin

# Node 22, as in ci/Dockerfile (the apps pin it).
RUN cd /tmp \
 && curl -fsSLO "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
 && curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt" | grep " node-v${NODE_VERSION}-linux-x64.tar.xz\$" | sha256sum -c - \
 && tar -xJf "node-v${NODE_VERSION}-linux-x64.tar.xz" -C /usr/local --strip-components=1 \
 && rm "node-v${NODE_VERSION}-linux-x64.tar.xz" \
 && node --version
