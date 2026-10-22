// Jenkins pipeline — capriole-corpus: the nightly corpus run (corpus-tools/nightly.py, docs/PLAN.md phase 4).
//
// A pipeline job of its own (not the multibranch one), on main: «Pipeline script from SCM», this repository,
// branch */main, script path ci/corpus.Jenkinsfile (docs/DEPLOY.md, «Job notturno dei corpora»). Every night
// one SafeDocs zip and one UNSAFE zip, downloaded, run through run_corpus.py under ASan/UBSan and deleted;
// on Mondays the three fixed corpora too, compared with their previous run. A crash fails the build, a
// regression makes it UNSTABLE; the summaries are the build's artifacts.
//
// The fixed corpora (~2.1 GB, downloaded on the first Monday) and the history of the reports live on the
// Jenkins host, in the directory of the global property CAPRIOLE_CORPUS_HOST_DIR (Manage Jenkins → System →
// Global properties), mounted as /corpora. The workspace is kept: build/corpus is the next night's cache
// (and its dependencies are not downloaded again); the night's zips are deleted by nightly.py itself.

pipeline {
  agent none

  options {
    disableConcurrentBuilds()
    timestamps()
    buildDiscarder(logRotator(numToKeepStr: '60'))
  }

  // H: Jenkins picks a fixed minute in the hour for this job, so it does not start with the others.
  triggers { cron('H 1 * * *') }

  environment {
    // The Jenkins host is shared with other projects: do not take every core.
    CMAKE_BUILD_PARALLEL_LEVEL = '4'
  }

  stages {
    stage('Setup') {
      steps {
        script {
          if (!env.CAPRIOLE_CORPUS_HOST_DIR) {
            error 'CAPRIOLE_CORPUS_HOST_DIR is not set (Manage Jenkins → System → Global properties): see docs/DEPLOY.md'
          }
        }
      }
    }

    stage('Corpus') {
      agent {
        dockerfile {
          filename 'corpus.Dockerfile'
          dir 'ci'
          args "-v ${env.CAPRIOLE_CORPUS_HOST_DIR}:/corpora -e CAPRIOLE_CORPUS_DIR=/corpora -e HOME=/tmp"
        }
      }
      steps {
        timeout(time: 6, unit: 'HOURS') {
          sh 'cmake --preset corpus && cmake --build --preset corpus'
          sh 'rm -rf corpus-reports'
          script {
            def status = sh(returnStatus: true, script: 'python3 -I corpus-tools/nightly.py --jobs 3 --work corpus-work --artifacts corpus-reports')
            if (status == 3) {
              unstable 'a regression against the previous run of a corpus: corpus-reports/summary.md'
            } else if (status == 1) {
              error 'a crash (sanitizer or signal): corpus-reports/summary.md'
            } else if (status != 0) {
              error "nightly.py exited ${status}: a corpus could not be run (download?), see the log"
            }
          }
        }
      }
      post {
        always {
          archiveArtifacts artifacts: 'corpus-reports/**', allowEmptyArchive: true
        }
        cleanup {
          sh 'rm -rf corpus-work'
        }
      }
    }
  }
}
