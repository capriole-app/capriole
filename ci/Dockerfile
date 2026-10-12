# CI image for Jenkins (agent { dockerfile }). Everything the gate needs, pinned, so the build does not
# depend on what happens to be installed on the Jenkins host. Keep versions in step with docs/SETUP.md.
FROM ubuntu:24.04

ARG EMSDK_VERSION=6.0.10

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 xz-utils \
      cmake ninja-build g++ \
      clang-20 clang-tidy-20 libclang-rt-20-dev lld-20 clang-format-18 \
 && ln -s /usr/bin/clang-format-18 /usr/local/bin/clang-format \
 && rm -rf /var/lib/apt/lists/*

# Emscripten, world-writable because Jenkins runs the container as its own uid and emcc writes its
# system-library cache on first use.
RUN git clone --depth 1 https://github.com/emscripten-core/emsdk.git /opt/emsdk \
 && /opt/emsdk/emsdk install "${EMSDK_VERSION}" \
 && /opt/emsdk/emsdk activate "${EMSDK_VERSION}" \
 && chmod -R a+rwX /opt/emsdk

ENV EMSDK=/opt/emsdk

# Node 22 for the apps (apps/inspector, apps/capriole pin it: .nvmrc, engines, engine-strict). The one in
# emsdk is Node 24 and stays emsdk's. The archive is checked against the official SHASUMS256.txt,
# fetched over TLS at build time — TODO: pin the hash here, as for the other dependencies.
ARG NODE_VERSION=22.23.3
RUN cd /tmp \
 && curl -fsSLO "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.xz" \
 && curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt" | grep " node-v${NODE_VERSION}-linux-x64.tar.xz\$" | sha256sum -c - \
 && tar -xJf "node-v${NODE_VERSION}-linux-x64.tar.xz" -C /usr/local --strip-components=1 \
 && rm "node-v${NODE_VERSION}-linux-x64.tar.xz" \
 && node --version
