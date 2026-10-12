#!/usr/bin/env bash
# The image's context, from the built app: deploy/web/context/ with build/ (apps/capriole/build, as the
# gate built it), csp.caddy (its policy, csp.mjs), the Caddyfile and the Dockerfile.
#
#   deploy/web/package.sh            then: docker build -t capriole-web deploy/web/context
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="$here/../../apps/capriole/build"
[[ -f "$build/index.html" ]] || { echo "✖ $build/index.html missing: build the app first (npm run build)" >&2; exit 1; }
rm -rf "$here/context"
mkdir -p "$here/context"
cp -r "$build" "$here/context/build"
node "$here/csp.mjs" "$build" > "$here/context/csp.caddy"
cp "$here/Caddyfile" "$here/Dockerfile" "$here/context/"
echo "✔ $here/context"
