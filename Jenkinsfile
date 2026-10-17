// Jenkins pipeline — capriole (C++ engine).
//
// The gate is the one a developer runs, invoked by name: scripts/check.sh (clang-format, GCC build
// with ASan/UBSan and -Werror, unit tests, clang-tidy, WebAssembly build + tests). Nothing here
// re-implements a check, so CI cannot drift into passing what the pre-push hook would fail.
// On top of it, CI adds what is too slow or too specific for every push: a Clang release build and
// a short libFuzzer run per target. On main, the web app is then deployed to capriole.app (an image on
// ghcr.io, pulled by contabo-b: docs/DEPLOY.md). On a release tag (v*): the desktop app's installers and
// the GitHub release, and the web app's image tagged with the version, for self-hosting (docs/DEPLOY.md,
// «Rilascio di una versione»; docs/SELF-HOSTED.md).
//
// Why a dockerfile agent: the toolchain (GCC 13, Clang 20, emsdk) is pinned in ci/Dockerfile, so the
// Jenkins host needs only docker. The pipeline starts nothing with a fixed name or host port, so it
// needs no lock(): each build lives in its own container and network namespace.

// A release tag is built once and never again: unlike a branch's, its workspace (the gate's build trees, the
// desktop's Rust target, node_modules: ~11 GB) is no cache for a next build, and two tags in a day filled the
// Jenkins host's disk. It goes at the end of the build: after the last stage (Image), or after the stage that
// failed, since the later ones do not run. Each stage has its own agent (agent none below), so each one can
// delete only the workspace it ran in, from its own post; the stages share it (Image builds the context
// that the earlier ones made), so none deletes it while a later stage still needs it.
def dropTagWorkspace(boolean lastStage) {
  if (env.TAG_NAME && (lastStage || currentBuild.currentResult != 'SUCCESS')) {
    deleteDir()
  }
}

pipeline {
  agent none

  options {
    disableConcurrentBuilds()
    timestamps()
    buildDiscarder(logRotator(numToKeepStr: '30'))
  }

  environment {
    CI = 'true'
    // The Jenkins host is shared with other projects: do not take every core.
    CMAKE_BUILD_PARALLEL_LEVEL = '4'
    // Where the web app is deployed (docs/DEPLOY.md). The server's address, SSH port and user are not in
    // this public file: they come from Jenkins (Manage Jenkins → System → Global properties:
    // CAPRIOLE_DEPLOY_HOST, CAPRIOLE_DEPLOY_PORT, CAPRIOLE_DEPLOY_USER); Deploy fails if one is missing.
    DEPLOY_DIR = '/srv/apps/capriole-web'
  }

  stages {
    stage('CI') {
      agent {
        dockerfile {
          filename 'ci/Dockerfile'
          dir '.'
          // Jenkins runs the container as its own uid, which has no home directory in the image.
          args '-e HOME=/tmp'
        }
      }

      stages {
        stage('Gate') {
          steps {
            timeout(time: 90, unit: 'MINUTES') {
              sh '''#!/usr/bin/env bash
                set -euo pipefail
                # The workspace is kept between builds: a file a past build generated where the tree no
                # longer ignores it (the engine module once lived in apps/inspector) would be checked as
                # source. Untracked files go; ignored ones (build trees, node_modules) stay as caches.
                git clean -fd
                # The resource pack (CJK fonts, CMaps, the CMYK profile: ADR 0005), so the web app the gate
                # builds — the one deployed — serves it. Pinned and checked; kept between builds (build/).
                scripts/fetch_resources.sh
                source "$EMSDK/emsdk_env.sh" >/dev/null
                SKIP_WASM=0 scripts/check.sh
              '''
            }
          }
        }

        stage('Release (Clang)') {
          steps {
            timeout(time: 20, unit: 'MINUTES') {
              sh '''#!/usr/bin/env bash
                set -euo pipefail
                cmake --preset release -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DCAPRIOLE_WARNINGS_AS_ERRORS=ON
                cmake --build --preset release
                ctest --preset release
              '''
            }
          }
        }

        stage('Fuzz smoke') {
          steps {
            // 34 targets: at 60 s each the fuzzing alone outlasted the stage. A smoke run is a short
            // pass on every target; the long campaigns run outside CI.
            timeout(time: 45, unit: 'MINUTES') {
              sh '''#!/usr/bin/env bash
                set -euo pipefail
                # A crash's stack trace must have function names: without the symbolizer it is addresses only.
                command -v llvm-symbolizer >/dev/null || { echo "[ci] ERROR: llvm-symbolizer not on PATH (ci/Dockerfile)"; exit 1; }
                cmake --preset fuzz
                cmake --build --preset fuzz
                mkdir -p fuzz/artifacts
                for target in build/fuzz/fuzz/fuzz_*; do
                  name=$(basename "$target"); name=${name#fuzz_}
                  mkdir -p "fuzz/corpus-work/$name"
                  "$target" -max_total_time=20 -artifact_prefix=fuzz/artifacts/ "fuzz/corpus-work/$name" "fuzz/corpus/$name"
                done
              '''
            }
          }
          post {
            failure {
              // The crashing input is the regression test to be: keep it.
              archiveArtifacts artifacts: 'fuzz/artifacts/*', allowEmptyArchive: true
            }
          }
        }

        // On main (it is reached from development by merge) and on a release tag: the web app the gate
        // built, with its Content-Security-Policy, as the image's context (deploy/web/package.sh needs node).
        stage('Web package') {
          when { anyOf { branch 'main'; tag 'v*' } }
          steps {
            sh 'deploy/web/package.sh'
          }
        }

        // On a release tag: the engine the gate built for WebAssembly, for the desktop app (stage «Release (tag)»).
        stage('Engine for the desktop (tag)') {
          when { tag 'v*' }
          steps {
            stash name: 'engine-wasm', includes: 'build/wasm/bindings/wasm/capriole.mjs,build/wasm/bindings/wasm/capriole.wasm'
          }
        }
      }
      post {
        cleanup { script { dropTagWorkspace(false) } }
      }
    }

    // On a release tag: the desktop app's installers (.deb, AppImage, Windows setup.exe cross-built) and the
    // web app as an archive for self-hosting (scripts/build_release.sh), in their own image
    // (ci/desktop.Dockerfile); kept as the build's artifacts, then published as the tag's GitHub release
    // (scripts/github_release.sh; credential github-release: Contents read and write on this repository).
    stage('Release (tag)') {
      when {
        beforeAgent true
        tag 'v*'
      }
      agent {
        dockerfile {
          filename 'desktop.Dockerfile'
          dir 'ci'
          args '-e HOME=/tmp'
        }
      }
      steps {
        timeout(time: 60, unit: 'MINUTES') {
          unstash 'engine-wasm'
          sh 'scripts/build_release.sh'
          archiveArtifacts artifacts: 'dist/release/*', fingerprint: true
          withCredentials([string(credentialsId: 'github-release', variable: 'GITHUB_TOKEN')]) {
            sh 'scripts/github_release.sh "$TAG_NAME" dist/release'
          }
        }
      }
      post {
        cleanup { script { dropTagWorkspace(false) } }
      }
    }

    // The image of the web app, tagged with the commit's sha (never `latest`), pushed to ghcr.io. On a
    // release tag, tagged with the version instead (v0.1.0 → 0.1.0): the image self-hosting uses.
    stage('Image') {
      when {
        beforeAgent true
        anyOf { branch 'main'; tag 'v*' }
      }
      agent any
      steps {
        timeout(time: 15, unit: 'MINUTES') {
          script {
            env.IMAGE_TAG = env.TAG_NAME ? env.TAG_NAME.substring(1) : env.GIT_COMMIT.take(7)
          }
          withCredentials([usernamePassword(credentialsId: 'ghcr-push', usernameVariable: 'GHCR_USER', passwordVariable: 'GHCR_TOKEN')]) {
            sh '''#!/usr/bin/env bash
              set -euo pipefail
              # A docker configuration of its own: the login does not touch jenkins' shared one.
              DOCKER_CONFIG="$(mktemp -d)"
              export DOCKER_CONFIG
              trap 'rm -rf "$DOCKER_CONFIG"' EXIT
              printf '%s' "$GHCR_TOKEN" | docker login ghcr.io -u "$GHCR_USER" --password-stdin
              IMAGE="ghcr.io/capriole-app/capriole-web:${IMAGE_TAG}"
              docker build --pull -t "$IMAGE" deploy/web/context
              docker push "$IMAGE"
              docker image rm -f "$IMAGE" >/dev/null 2>&1 || true
            '''
          }
        }
      }
      post {
        cleanup { script { dropTagWorkspace(true) } }
      }
    }

    // capriole.app: the server pulls that image (./deploy.sh, the shape the server's SSH gate allows) and waits for it to be healthy.
    stage('Deploy (main)') {
      when {
        beforeAgent true
        branch 'main'
      }
      agent any
      steps {
        timeout(time: 10, unit: 'MINUTES') {
          // contabo-b's host key is pinned: another server is refused.
          withCredentials([file(credentialsId: 'contabo-b-known-hosts', variable: 'KNOWN_HOSTS')]) {
            sshagent(credentials: ['jenkins-master-ssh-key']) {
              sh '''#!/usr/bin/env bash
                set -euo pipefail
                [ -n "${IMAGE_TAG:-}" ] || { echo "[ci] ERROR: IMAGE_TAG missing (did Image fail?)"; exit 1; }
                for v in CAPRIOLE_DEPLOY_HOST CAPRIOLE_DEPLOY_PORT CAPRIOLE_DEPLOY_USER; do
                  [ -n "${!v:-}" ] || { echo "[ci] ERROR: $v is not set in Jenkins (Global properties)"; exit 1; }
                done
                ssh -p "$CAPRIOLE_DEPLOY_PORT" -o BatchMode=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$KNOWN_HOSTS" \
                  -o ConnectTimeout=10 -o ServerAliveInterval=15 -o ServerAliveCountMax=2 \
                  "$CAPRIOLE_DEPLOY_USER@$CAPRIOLE_DEPLOY_HOST" \
                  "set -euo pipefail; cd '$DEPLOY_DIR'; ./deploy.sh --env main --tag '$IMAGE_TAG'"
              '''
            }
          }
        }
      }
    }
  }
}
