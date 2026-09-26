// Jenkins pipeline — capriole (C++ engine).
//
// The gate is the one a developer runs, invoked by name: scripts/check.sh (clang-format, GCC build
// with ASan/UBSan and -Werror, unit tests, clang-tidy, WebAssembly build + tests). Nothing here
// re-implements a check, so CI cannot drift into passing what the pre-push hook would fail.
// On top of it, CI adds what is too slow or too specific for every push: a Clang release build and
// a short libFuzzer run per target.
//
// Why a dockerfile agent: the toolchain (GCC 13, Clang 20, emsdk) is pinned in ci/Dockerfile, so the
// Jenkins host needs only docker. The pipeline starts nothing with a fixed name or host port, so it
// needs no lock(): each build lives in its own container and network namespace.

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
            timeout(time: 30, unit: 'MINUTES') {
              sh '''#!/usr/bin/env bash
                set -euo pipefail
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
            timeout(time: 20, unit: 'MINUTES') {
              sh '''#!/usr/bin/env bash
                set -euo pipefail
                cmake --preset fuzz
                cmake --build --preset fuzz
                mkdir -p fuzz/artifacts
                for target in build/fuzz/fuzz/fuzz_*; do
                  name=$(basename "$target"); name=${name#fuzz_}
                  mkdir -p "fuzz/corpus-work/$name"
                  "$target" -max_total_time=60 -artifact_prefix=fuzz/artifacts/ "fuzz/corpus-work/$name" "fuzz/corpus/$name"
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
      }
    }
  }
}
