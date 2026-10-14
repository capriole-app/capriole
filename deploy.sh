#!/usr/bin/env bash
#
# deploy.sh — the entry point Jenkins uses on contabo-b, in the shape every project shares:
#
#   cd /srv/apps/capriole-web && ./deploy.sh --env main --tag <sha>
#
# The server's SSH gate lets the deploy key run that line and nothing else. The work is done by
# deploy/web/deploy.sh; this only checks --env and hands over with exec, so the checkout that script
# makes cannot rewrite a file bash is still reading.
set -euo pipefail

args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --env)   ENV_ARG="${2:?--env needs a value}"; shift 2 ;;
    --env=*) ENV_ARG="${1#*=}"; shift ;;
    *) args+=("$1"); shift ;;
  esac
done
if [[ "${ENV_ARG:-main}" != main ]]; then
  echo "✖ --env ${ENV_ARG}: capriole has only main." >&2
  exit 1
fi

exec "$(dirname "$0")/deploy/web/deploy.sh" "${args[@]}"
