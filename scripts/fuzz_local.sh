#!/usr/bin/env bash
# Low-intensity fuzzing campaign on this machine, in the background (PLAN.md phase 2 exit: 24 h).
#
#   scripts/fuzz_local.sh start [hours]   build the fuzzers and start the campaign (default 24 h)
#   scripts/fuzz_local.sh status          is it running, and the progress so far
#   scripts/fuzz_local.sh stop            stop it (the accumulated corpus is kept)
#   scripts/fuzz_local.sh report          summary and findings of the last campaign
#
# Low intensity by construction — it must not get in the way of normal work:
#   - a user systemd unit (capriole-fuzz) with hard cgroup limits: 1 CPU core (CPUQuota=100%),
#     1.5 GB of memory and no swap, nice 19, idle I/O priority;
#   - one fuzzer at a time (scripts/fuzz_campaign.sh <hours> 1), 1 GB RSS cap per fuzzer;
#   - it runs from its own worktree (.git/capriole-fuzz-worktree) at the commit it was started on,
#     so rebuilding or switching branches in the main checkout does not touch the running binaries.
# The unit belongs to the user session: it stops at logout unless lingering is enabled
# (loginctl enable-linger).
set -euo pipefail

unit=capriole-fuzz
common_dir="$(git rev-parse --path-format=absolute --git-common-dir)"
worktree="$common_dir/capriole-fuzz-worktree"

case "${1:-}" in
  start)
    hours="${2:-24}"
    if systemctl --user is-active --quiet "$unit"; then
      echo "a campaign is already running: scripts/fuzz_local.sh status" >&2
      exit 1
    fi
    ref="$(git rev-parse HEAD)"
    if [[ ! -e "$worktree/.git" ]]; then
      git worktree prune
      git worktree add --detach --quiet "$worktree" "$ref"
    fi
    git -C "$worktree" checkout --detach --force --quiet "$ref"
    git -C "$worktree" clean -fdq  # ignored paths (build/, fuzz/corpus-work/) survive
    systemctl --user reset-failed "$unit" 2>/dev/null || true
    systemd-run --user --unit="$unit" --collect --quiet \
      -p CPUQuota=100% -p MemoryMax=1536M -p MemorySwapMax=0 -p Nice=19 -p IOSchedulingClass=idle \
      -p WorkingDirectory="$worktree" \
      -E PATH="$PATH" -E RSS_LIMIT_MB=1024 -E CMAKE_BUILD_PARALLEL_LEVEL=1 \
      -- bash -c 'cmake --preset fuzz >/dev/null && cmake --build --preset fuzz >/dev/null && exec scripts/fuzz_campaign.sh "$0" 1' "$hours"
    echo "started: $hours h at $(git log -1 --format='%h %s' "$ref")"
    echo "follow:  scripts/fuzz_local.sh status   (stop: scripts/fuzz_local.sh stop)"
    ;;
  status)
    if systemctl --user is-active --quiet "$unit"; then
      echo "running since $(systemctl --user show "$unit" -p ActiveEnterTimestamp --value)"
    else
      echo "not running"
    fi
    [[ -f "$worktree/fuzz/campaign/summary.txt" ]] && { echo "finished targets:"; cat "$worktree/fuzz/campaign/summary.txt"; }
    current=$(ls -t "$worktree"/fuzz/campaign/*.log 2>/dev/null | head -1 || true)
    [[ -n "$current" ]] && echo "current: $(basename "$current" .log) — $(grep -aoE '#[0-9]+ .*(cov|exec/s)[^\r]*' "$current" | tail -1 | cut -c1-120)"
    echo "findings so far: $(find "$worktree/fuzz/campaign/crashes" -type f 2>/dev/null | wc -l)"
    ;;
  stop)
    systemctl --user stop "$unit" && echo "stopped (corpus kept in $worktree/fuzz/corpus-work)"
    ;;
  report)
    cat "$worktree/fuzz/campaign/summary.txt" 2>/dev/null || echo "no finished campaign yet"
    find "$worktree/fuzz/campaign/crashes" -type f 2>/dev/null
    journalctl --user -u "$unit" --no-pager -n 20 -q 2>/dev/null | tail -20
    ;;
  *)
    sed -n '2,8p' "$0"
    exit 2
    ;;
esac
