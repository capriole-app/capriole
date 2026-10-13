#!/usr/bin/env bash
#
# stats.sh — how much capriole.app is used: the openings of the page per day, counted from the anonymous
# log the web server keeps (Caddyfile: no address, no path, only the time, the status and the browser's
# name). An opening is a person loading the page, again on each reload: people are not told apart,
# since nothing that could tell them apart is kept.
#
#   deploy/web/stats.sh                  reads the log in the container, adds its days to the record and
#                                        prints the last 30 (on contabo-b, once a day by cron: docs/DEPLOY.md)
#   deploy/web/stats.sh --from-file F    counts the lines of F and only prints them (test.sh)
#
# The record (CAPRIOLE_STATS, by default ~/capriole-visits.csv): day,visits,robots, in UTC days. The log's
# lines roll away (Caddyfile: 10 files of 10 MiB), so a day's numbers in the record only ever grow.
set -euo pipefail

# Robots: search engines, link previews, monitors, scripts — and a request with no browser's name.
count() {
  awk '
    /"logger":"http\.log\.access/ && /"method":"GET"/ && /"status":(200|304)[,}]/ {
      if (!match($0, /"ts":"[0-9]+-[0-9]+-[0-9]+/)) next
      day = substr($0, RSTART + 6, 10)
      ua = ""
      if (match($0, /"ua":"[^"]*/)) ua = tolower(substr($0, RSTART + 6, RLENGTH - 6))
      if (ua == "" || ua ~ /bot|crawl|spider|slurp|preview|fetch|monitor|uptime|curl|wget|python|go-http|java\/|okhttp|headless|lighthouse|facebookexternalhit|scan/) robots[day]++
      else visits[day]++
      days[day] = 1
    }
    END { for (d in days) printf "%s,%d,%d\n", d, visits[d], robots[d] }
  ' | sort
}

show() {
  awk -F, '{ printf "%-12s %8s %8s\n", $1, $2, $3 }'
}

if [[ "${1:-}" == --from-file ]]; then
  { echo "day,visits,robots"; count <"${2:?--from-file needs a file}"; } | show
  exit 0
fi
[[ $# -eq 0 ]] || { echo "✖ unknown argument: $1" >&2; exit 1; }

record="${CAPRIOLE_STATS:-$HOME/capriole-visits.csv}"
fresh="$(docker exec capriole-web sh -c 'cat /logs/*.log 2>/dev/null || true' | count)"
touch "$record"
merged="$(
  { tail -n +2 "$record"; printf '%s\n' "$fresh"; } | awk -F, '
    NF == 3 {
      if (!($1 in v) || $2 > v[$1]) v[$1] = $2
      if (!($1 in r) || $3 > r[$1]) r[$1] = $3
    }
    END { for (d in v) printf "%s,%d,%d\n", d, v[d], r[d] }
  ' | sort
)"
{ echo "day,visits,robots"; [[ -z "$merged" ]] || printf '%s\n' "$merged"; } >"$record.tmp"
mv "$record.tmp" "$record"
{ head -1 "$record"; tail -n +2 "$record" | tail -n 30; } | show
