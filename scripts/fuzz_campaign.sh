#!/usr/bin/env bash
# Long fuzzing campaign over every libFuzzer target (DOCS/CAPRIOLE.md §7.4, PLAN.md phase 2 exit).
#
#   scripts/fuzz_campaign.sh <hours> <workers>
#
# Runs all targets built by the `fuzz` preset for <hours> of wall-clock time, <workers> at a time, at
# the lowest CPU priority. Each target gets an equal share: hours × workers / targets. Working
# corpora persist in fuzz/corpus-work/<target> (seeded from fuzz/corpus/<target>), so successive
# campaigns build on each other. Crashing inputs, logs and a summary go to fuzz/campaign/.
# Exits 1 if any target found a crash, leak, timeout or OOM.
# RSS_LIMIT_MB (default 2048) caps each fuzzer's memory; exceeding it is reported as a finding.
set -euo pipefail
cd "$(dirname "$0")/.."

hours=${1:?hours}
workers=${2:?workers}
build=build/fuzz/fuzz
out=fuzz/campaign
mkdir -p "$out"
rm -f "$out"/*.log "$out"/summary.txt

# A target counts only if it has a seed corpus: stale binaries left in the build tree by renamed
# targets are ignored.
mapfile -t targets < <(find "$build" -maxdepth 1 -type f -name 'fuzz_*' -perm -u+x -printf '%f\n' | sort |
  while read -r t; do [[ -d "fuzz/corpus/${t#fuzz_}" ]] && echo "$t"; done)
[[ ${#targets[@]} -gt 0 ]] || { echo "no fuzz targets in $build: run cmake --preset fuzz && cmake --build --preset fuzz" >&2; exit 2; }
seconds=$(python3 -c "print(max(60, int($hours * 3600 * $workers / ${#targets[@]})))")
echo "campaign: ${#targets[@]} targets, $workers at a time, ${seconds}s each (≈ $hours h wall clock)"

run_target() {
  local target=$1 seconds=$2 build=$3 out=$4
  local name=${target#fuzz_}
  mkdir -p "fuzz/corpus-work/$name" "$out/crashes"
  # -rss_limit_mb and -timeout turn memory blow-ups and hangs into reported findings.
  nice -n 19 "$build/$target" -max_total_time="$seconds" -rss_limit_mb="${RSS_LIMIT_MB:-2048}" -timeout=30 -print_final_stats=1 \
    -artifact_prefix="$out/crashes/$name-" "fuzz/corpus-work/$name" "fuzz/corpus/$name" >"$out/$name.log" 2>&1
  local status=$?
  local runs
  runs=$(grep -oE 'stat::number_of_executed_units: [0-9]+' "$out/$name.log" | awk '{print $2}')
  echo "$name: exit $status, ${runs:-?} executions" >>"$out/summary.txt"
}
export -f run_target

printf '%s\n' "${targets[@]}" | xargs -P "$workers" -I{} bash -c 'run_target "$@"' _ {} "$seconds" "$build" "$out" || true

sort -o "$out/summary.txt" "$out/summary.txt"
cat "$out/summary.txt"
findings=$(find "$out/crashes" -type f 2>/dev/null | wc -l)
failed=$(grep -cv ': exit 0,' "$out/summary.txt" || true)
echo "findings: $findings, targets that did not finish cleanly: $failed"
[[ "$findings" -eq 0 && "$failed" -eq 0 ]]
