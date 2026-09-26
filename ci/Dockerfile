# CI image for Jenkins (agent { dockerfile }). Everything the gate needs, pinned, so the build does not
# depend on what happens to be installed on the Jenkins host. Keep versions in step with docs/SETUP.md.
FROM ubuntu:24.04

ARG EMSDK_VERSION=6.0.10

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates git python3 xz-utils \
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
