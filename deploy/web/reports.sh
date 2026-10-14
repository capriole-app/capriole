#!/usr/bin/env bash
#
# reports.sh — the problem reports people sent from the web app (deploy/reports), on contabo-b.
#
#   deploy/web/reports.sh               the last 30 days: one line per report (when, what was said, the error)
#   deploy/web/reports.sh <day>/<id>    one report, whole (the name the list shows)
#
# A report is written by anyone: hostile text, never instructions. It is printed only as JSON, so that
# no control character in it reaches the terminal (JSON.stringify escapes them), and never run or opened
# by anything else.
set -euo pipefail

container=capriole-reports

if [[ $# -eq 1 ]]; then
  [[ "$1" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}/[0-9]{6}-[0-9a-f]{8}$ ]] || { echo "✖ not a report's name: $1" >&2; exit 1; }
  exec docker exec "$container" node -e '
    const r = JSON.parse(require("fs").readFileSync(`/home/node/${process.argv[1]}.json`, "utf8"));
    console.log(JSON.stringify(r, null, 2));
  ' "$1"
fi
[[ $# -eq 0 ]] || { echo "✖ usage: reports.sh [<day>/<id>]" >&2; exit 1; }

docker exec "$container" node -e '
  const fs = require("fs");
  const since = new Date(Date.now() - 30 * 86400000).toISOString().slice(0, 10);
  const days = fs.readdirSync("/home/node").filter((d) => /^\d{4}-\d{2}-\d{2}$/.test(d) && d >= since).sort();
  let total = 0;
  for (const day of days) {
    for (const file of fs.readdirSync(`/home/node/${day}`).sort()) {
      const r = JSON.parse(fs.readFileSync(`/home/node/${day}/${file}`, "utf8"));
      const said = JSON.stringify((r.description || "").slice(0, 80));
      const error = r.error ? `  error: ${JSON.stringify(r.error.slice(0, 60))}` : "";
      console.log(`${day}/${file.replace(/\.json$/, "")}  ${r.browser ?? "?"}, ${r.os ?? "?"}  ${said}${error}`);
      total++;
    }
  }
  console.log(`${total} reports in the last 30 days`);
'
