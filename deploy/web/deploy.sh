#!/usr/bin/env bash
#
# deploy.sh — the web app (capriole.app) on the VPS (contabo-b).
#
# Jenkins builds the image and pushes it to ghcr.io with the commit's sha: here it is only pulled.
# The VPS builds nothing.
#
#   --tag <sha>   deploys that image, and brings the checkout to the same commit
#   (no --tag)    deploys again the IMAGE_TAG written in .env
#
# Jenkins runs it on the server so:
#   cd /srv/apps/capriole-web && ./deploy.sh --env main --tag <sha>   (the root wrapper)
#
# Going back: deploy/web/deploy.sh --tag <previous sha> (printed at each run).
# The FIRST deploy is by hand, after the `git clone` (docs/DEPLOY.md).
#
# Mind the brace below and its partner at the end. With --tag this script checks out a commit that
# contains the script itself, so it rewrites its own file while running: the block makes bash read
# the whole file before running its first line.
#
{
set -euo pipefail

IMAGE_TAG_ARG=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)   IMAGE_TAG_ARG="${2:?--tag needs a value}"; shift 2 ;;
    --tag=*) IMAGE_TAG_ARG="${1#*=}"; shift ;;
    *) echo "✖ unknown argument: $1" >&2; exit 1 ;;
  esac
done

# The checkout's root (the script is in deploy/web/), and compose's folder.
cd "$(dirname "$0")/../.."
REPO="$(pwd)"
WEB="$REPO/deploy/web"
COMPOSE=(docker compose --project-directory "$WEB" -f "$WEB/docker-compose.yml")
echo "▶ [capriole-web] deploy in $REPO"

# git must never stop on a question inside the pipeline. core.sshCommand, when set, carries this
# repo's deploy key: kept, since GIT_SSH_COMMAND would take precedence and silently override it.
GIT_SSH_COMMAND="$(git config --get core.sshCommand || echo ssh) -o BatchMode=yes"
export GIT_SSH_COMMAND

touch "$WEB/.env"
PREVIOUS_TAG="$(sed -n 's/^IMAGE_TAG=//p' "$WEB/.env" | tail -1)"

if [[ -n "$IMAGE_TAG_ARG" ]]; then
  IMAGE_TAG="$IMAGE_TAG_ARG"
else
  if [[ -z "$PREVIOUS_TAG" ]]; then
    echo "✖ no --tag and no IMAGE_TAG in deploy/web/.env: the first deploy needs one (--tag <sha>)." >&2
    exit 1
  fi
  IMAGE_TAG="$PREVIOUS_TAG"
  echo "▶ no --tag: deploying the current image again ($IMAGE_TAG)"
fi
echo "▶ IMAGE_TAG: $IMAGE_TAG"
if [[ -n "$PREVIOUS_TAG" && "$PREVIOUS_TAG" != "$IMAGE_TAG" ]]; then
  echo "  previous tag: $PREVIOUS_TAG   (back: deploy/web/deploy.sh --tag $PREVIOUS_TAG)"
fi

# The compose file comes from the image's commit: an old image with a new compose is exactly how a
# rollback breaks. Detached HEAD on purpose: a server runs a version, not a branch.
if [[ -n "$IMAGE_TAG_ARG" ]]; then
  if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "✖ local changes in $REPO: the checkout would overwrite them. See: git status" >&2
    exit 1
  fi
  echo "▶ git fetch + checkout $IMAGE_TAG"
  git fetch --prune --tags origin
  if ! git rev-parse --verify --quiet "${IMAGE_TAG}^{commit}" >/dev/null; then
    echo "✖ commit $IMAGE_TAG not found after the fetch." >&2
    exit 1
  fi
  git checkout --detach --quiet "$IMAGE_TAG"
  echo "  HEAD -> $(git log -1 --format='%h %s')"
fi

# IMAGE_TAG in deploy/web/.env (untracked: it survives the checkout), from which compose takes the tag.
[[ -n "$(tail -c1 "$WEB/.env")" ]] && echo >> "$WEB/.env"
if grep -q '^IMAGE_TAG=' "$WEB/.env"; then
  sed -i "s|^IMAGE_TAG=.*|IMAGE_TAG=${IMAGE_TAG}|" "$WEB/.env"
else
  echo "IMAGE_TAG=${IMAGE_TAG}" >> "$WEB/.env"
fi

echo "▶ docker compose pull"
"${COMPOSE[@]}" pull

# --wait waits for the container to be healthy (the image's probe): without it `up -d` returns as soon
# as the container starts, and a server that falls over would count as deployed.
echo "▶ docker compose up -d --remove-orphans --wait"
"${COMPOSE[@]}" up -d --remove-orphans --wait --wait-timeout 120

# The reports' service runs deploy/reports/server.mjs from the checkout, which compose does not see:
# a new script and the same compose leave the old one running. Started again, with the commit's script.
echo "▶ docker compose up -d --force-recreate reports"
"${COMPOSE[@]}" up -d --force-recreate --no-deps --wait --wait-timeout 60 reports

docker image prune -f >/dev/null 2>&1 || true

echo "▶ state"
"${COMPOSE[@]}" ps
echo "✔ [capriole-web] deployed $IMAGE_TAG"
} # end of the block: see the note at the top
