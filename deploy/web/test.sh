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
reports="capriole-reports-test-$$"
network="capriole-test-$$"
# The reports' service as compose runs it (docker-compose.yml): Node's image, this checkout's script.
node_image="$(sed -n "s/^    image: '\(node:[^']*\)'$/\1/p" "$here/docker-compose.yml")"
[[ -n "$node_image" ]] || { echo "✖ no node image in docker-compose.yml" >&2; exit 1; }

node --test "$here/../reports/server.test.mjs" >/dev/null || { echo "✖ deploy/reports: its tests fail" >&2; exit 1; }
echo "✔ reports' service: its tests"

"$here/package.sh"
docker build -q -t "$image" "$here/context" >/dev/null
trap 'docker rm -f "$name" "$reports" >/dev/null 2>&1 || true; docker network rm "$network" >/dev/null 2>&1 || true' EXIT
docker network create "$network" >/dev/null
docker run -d --name "$reports" --network "$network" --network-alias capriole-reports --user 1000:1000 --read-only \
  --tmpfs /tmp:size=8m --tmpfs /home/node:size=4m,uid=1000,gid=1000 -e REPORTS_DIR=/home/node \
  -v "$here/../reports/server.mjs:/app/server.mjs:ro" \
  --cap-drop ALL --security-opt no-new-privileges:true --memory 128m "$node_image" node /app/server.mjs >/dev/null
docker run -d --name "$name" --network "$network" -p "127.0.0.1:$port:8080" --read-only \
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
# A language's page (/it/: src/lib/page-head.ts), with the same policy, its own head and a working app.
it="$(curl -fsS -D - "$url/it/")"
grep -qi "^content-security-policy: .*'wasm-unsafe-eval' 'sha256-" <<<"$it" || fail "no CSP with hashes on /it/"
grep -q '<html lang="it"' <<<"$it" || fail "/it/ is not in Italian"
grep -q '<link rel="canonical" href="https://capriole.app/it/" />' <<<"$it" || fail "/it/ has not its canonical address"
[[ "$(curl -sS -o /dev/null -w '%{http_code}' "$url/xx/")" == 404 ]] || fail "a language the app does not speak is served"
echo "✔ headers"

# The count of visits: an opening of the page and a start of the app (its logo), with an address
# forwarded and a query; each line with no address, no path and no header but the browser's name.
curl -fsS -o /dev/null -A 'capriole-test' -H 'X-Forwarded-For: 203.0.113.7' "$url/?from=test"
curl -fsS -o /dev/null -A 'capriole-test' "$url/it/"
curl -fsS -o /dev/null -A 'capriole-test' "$url/logo-light.svg"
sleep 0.5
log="$(docker exec "$name" cat /logs/access.log)"
grep -q '"method":"GET".*"status":200.*"ua":"capriole-test","kind":"page"' <<<"$log" || fail "an opening is not counted: $log"
grep -q '"method":"GET".*"status":200.*"ua":"capriole-test","kind":"start"' <<<"$log" || fail "a start is not counted: $log"
grep -qE '203\.0\.113|from=test|/it/|remote_ip|client_ip|"headers"|"uri"|wasm' <<<"$log" && fail "the count keeps more than it should: $log"
counted="$("$here/stats.sh" --from-file <(printf '%s\n' "$log"))" || fail "stats.sh cannot read the count"
# A start, two openings (/ and /it/) by a browser, one by a robot (curl, reading /it/'s head above).
grep -qE '^[0-9-]+ +1 +2 +1$' <<<"$counted" || fail "stats.sh counts wrong: $counted"
echo "✔ anonymous count of visits"

# A problem report, through the app's Caddy as the browser sends it: kept, with no address; anything
# posted elsewhere, or too large, refused.
answer="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'content-type: application/json' \
  -H 'X-Forwarded-For: 203.0.113.9' --data '{"v":1,"description":"test report","platform":"web","secret":"x"}' "$url/api/report")"
[[ "$answer" == 201 ]] || { docker logs "$reports"; fail "a report is not received: $answer"; }
kept="$(docker exec "$reports" sh -c 'cat /home/node/*/*.json')"
grep -q '"description": "test report"' <<<"$kept" || fail "the report is not kept: $kept"
grep -qE '203\.0\.113|secret' <<<"$kept" && fail "the report keeps more than it should: $kept"
big="$(head -c 20000 /dev/zero | tr '\0' 'a')"
answer="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'content-type: application/json' \
  --data "{\"v\":1,\"description\":\"$big\"}" "$url/api/report")"
[[ "$answer" == 413 ]] || fail "a report too large is not refused: $answer"
answer="$(curl -sS -o /dev/null -w '%{http_code}' -X POST --data 'x' "$url/")"
[[ "$answer" != 2* ]] || fail "something else can be posted: $answer"
echo "✔ problem reports"

# Caddy's own log of an error (the reports' service gone: 502), on the container's output: what failed is
# there, the client's address and headers are not.
docker stop -t 1 "$reports" >/dev/null
answer="$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H 'content-type: application/json' -A 'capriole-test' \
  -H 'X-Forwarded-For: 203.0.113.11' --data '{"v":1,"description":"x"}' "$url/api/report")"
[[ "$answer" == 502 ]] || fail "without the reports' service, a report is not answered 502: $answer"
sleep 0.5
errors="$(docker logs "$name" 2>&1 | grep '"level":"error"')"
grep -q '"uri":"/api/report"' <<<"$errors" || fail "the error is not logged: $errors"
grep -qE '203\.0\.113|remote_ip|client_ip|"headers"|capriole-test' <<<"$errors" && fail "the error log keeps the client: $errors"
echo "✔ error log without the client"

cd "$here/../../apps/capriole"
CAPRIOLE_E2E_URL="$url" npx playwright test
echo "✔ capriole-web: the whole app works as served"
