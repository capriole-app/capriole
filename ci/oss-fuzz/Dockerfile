# OSS-Fuzz project image for Capriole (copied, with build.sh and project.yaml, into the oss-fuzz repository
# as projects/capriole/ — docs/PUBBLICAZIONE.md). The build needs CMake ≥ 3.28 (newer than the image's)
# and Ninja; the dependencies' archives are fetched here, pinned by SHA-256, because build.sh runs offline.
FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y ninja-build zip && pip3 install "cmake>=3.28,<4"
RUN git clone --depth 1 --branch development https://github.com/capriole-app/capriole.git capriole
RUN python3 capriole/ci/oss-fuzz/fetch_deps.py $SRC/capriole-deps
WORKDIR $SRC/capriole
COPY build.sh $SRC/
