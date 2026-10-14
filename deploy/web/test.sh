#!/usr/bin/env bash
# The image as it will run on contabo-b, tried locally: built from the app the gate built, started with
# the same hardening as docker-compose.yml (read-only, no capability, no new privileges, user 65532),
# its headers checked, and the app's whole end-to-end suite run against it — which is what proves the
# Content-Security-Policy lets everything work (the engine's WebAssembly and worker, the theme, the
# dialogs).
#
#   deploy/web/test.sh        (after `npm run build` in apps/capriole; needs docker and Playwright)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
port="${PORT:-8088}"
image="capriole-web:test"
name="capriole-web-test-$$"

"$here/package.sh"
docker build -q -t "$image" "$here/context" >/dev/null
trap 'docker rm -f "$name" >/dev/null 2>&1 || true' EXIT
docker run -d --name "$name" -p "127.0.0.1:$port:8080" --read-only \
  --tmpfs /tmp:size=16m --tmpfs /config:size=4m,uid=65532,gid=65532 --tmpfs /data:size=4m,uid=65532,gid=65532 --tmpfs /logs:size=4m,uid=65532,gid=65532 \
  --cap-drop ALL --security-opt no-new-privileges:true --memory 256m "$image" >/dev/null

url="http://127.0.0.1:$port"
for _ in $(seq 50); do curl -fsS "$url/healthz" >/dev/null 2>&1 && break; sleep 0.2; done
curl -fsS "$url/healthz" >/dev/null || { docker logs "$name"; echo "✖ not healthy" >&2; exit 1; }

fail() { echo "✖ $*" >&2; exit 1; }
head="$(curl -fsSI "$url/")"
grep -qi "^content-security-policy: .*'wasm-unsafe-eval' 'sha256-" <<<"$head" || fail "no CSP with hashes on /"
grep -qi '^x-content-type-options: nosniff' <<<"$head" || fail "no nosniff"
grep -qi '^cache-control: no-cache' <<<"$head" || fail "the page must be asked again (no-cache)"
grep -qi '^server:' <<<"$head" && fail "the Server header is not taken away"
asset="$(cd "$here/context/build" && find _app/immutable -name '*.wasm' | head -1)"
[[ -n "$asset" ]] || fail "no .wasm in _app/immutable"
wasm="$(curl -fsSI -H 'Accept-Encoding: gzip, zstd' "$url/$asset")"
grep -qi '^content-type: application/wasm' <<<"$wasm" || fail "the engine is not served as application/wasm"
grep -qi '^cache-control: public, max-age=31536000, immutable' <<<"$wasm" || fail "hashed files not kept"
grep -qi '^content-encoding: \(zstd\|gzip\)' <<<"$wasm" || fail "the engine is not compressed"
echo "✔ headers"

# The count of visits: an opening of the page and a start of the app (its logo), with an address
# forwarded and a query; each line with no address, no path and no header but the browser's name.
curl -fsS -o /dev/null -A 'capriole-test' -H 'X-Forwarded-For: 203.0.113.7' "$url/?from=test"
curl -fsS -o /dev/null -A 'capriole-test' "$url/logo-light.svg"
sleep 0.5
log="$(docker exec "$name" cat /logs/access.log)"
grep -q '"method":"GET".*"status":200.*"ua":"capriole-test","kind":"page"' <<<"$log" || fail "an opening is not counted: $log"
grep -q '"method":"GET".*"status":200.*"ua":"capriole-test","kind":"start"' <<<"$log" || fail "a start is not counted: $log"
grep -qE '203\.0\.113|from=test|remote_ip|client_ip|"headers"|"uri"|wasm' <<<"$log" && fail "the count keeps more than it should: $log"
counted="$("$here/stats.sh" --from-file <(printf '%s\n' "$log"))" || fail "stats.sh cannot read the count"
grep -qE '^[0-9-]+ +1 +1 +0$' <<<"$counted" || fail "stats.sh counts wrong: $counted"
echo "✔ anonymous count of visits"

cd "$here/../../apps/capriole"
CAPRIOLE_E2E_URL="$url" npx playwright test
echo "✔ capriole-web: the whole app works as served"
