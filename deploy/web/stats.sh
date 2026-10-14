#!/usr/bin/env bash
#
# stats.sh — how much capriole.app is used, per day, counted from the anonymous log the web server keeps
# (Caddyfile: no address, no path, only the time, the status, what was asked for and the browser's name):
#
#   starts   the app started in a browser (it asked for the first screen's logo): the people, near enough
#   pages    the page opened by anything that calls itself a browser: people, and the scanners that
#            pretend to be one (a new domain draws many; they fetch the page and never start the app)
#   robots   the page opened by what says it is a robot: search engines, previews, scripts
#
# Each reload counts again: people are not told apart, since nothing that could tell them apart is kept.
#
#   deploy/web/stats.sh                  reads the log in the container, adds its days to the record and
#                                        prints the last 30 (on contabo-b, once a day by cron: docs/DEPLOY.md)
#   deploy/web/stats.sh --from-file F    counts the lines of F and only prints them (test.sh)
#
# The record (CAPRIOLE_STATS, by default ~/capriole-usage.csv): day,starts,pages,robots, in UTC days. The
# log's lines roll away (Caddyfile: 10 files of 10 MiB), so a day's numbers in the record only ever grow.
set -euo pipefail

header="day,starts,pages,robots"

# A line with no label is an opening of the page (logged before starts were counted).
count() {
  awk '
    /"logger":"http\.log\.access/ && /"method":"GET"/ && /"status":(200|304)[,}]/ {
      if (!match($0, /"ts":"[0-9]+-[0-9]+-[0-9]+/)) next
      day = substr($0, RSTART + 6, 10)
      ua = ""
      if (match($0, /"ua":"[^"]*/)) ua = tolower(substr($0, RSTART + 6, RLENGTH - 6))
      robot = ua == "" || ua ~ /bot|crawl|spider|slurp|preview|fetch|monitor|uptime|curl|wget|python|go-http|java\/|okhttp|headless|lighthouse|facebookexternalhit|scan|checker|compatible;/
      days[day] = 1
      if (/"kind":"start"/) { if (!robot) starts[day]++ }
      else if (robot) robots[day]++
      else pages[day]++
    }
    END { for (d in days) printf "%s,%d,%d,%d\n", d, starts[d], pages[d], robots[d] }
  ' | sort
}

show() {
  awk -F, '{ printf "%-12s %8s %8s %8s\n", $1, $2, $3, $4 }'
}

if [[ "${1:-}" == --from-file ]]; then
  { echo "$header"; count <"${2:?--from-file needs a file}"; } | show
  exit 0
fi
[[ $# -eq 0 ]] || { echo "✖ unknown argument: $1" >&2; exit 1; }

record="${CAPRIOLE_STATS:-$HOME/capriole-usage.csv}"
fresh="$(docker exec capriole-web sh -c 'cat /logs/*.log 2>/dev/null || true' | count)"
touch "$record"
merged="$(
  { tail -n +2 "$record"; printf '%s\n' "$fresh"; } | awk -F, '
    NF == 4 {
      for (i = 2; i <= 4; i++) if (!(($1, i) in n) || $i > n[$1, i]) n[$1, i] = $i
      days[$1] = 1
    }
    END { for (d in days) printf "%s,%d,%d,%d\n", d, n[d, 2], n[d, 3], n[d, 4] }
  ' | sort
)"
{ echo "$header"; [[ -z "$merged" ]] || printf '%s\n' "$merged"; } >"$record.tmp"
mv "$record.tmp" "$record"
{ head -1 "$record"; tail -n +2 "$record" | tail -n 30; } | show
