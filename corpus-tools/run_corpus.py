#!/usr/bin/env python3
"""Differential corpus runner (DOCS/CAPRIOLE.md §7.2-7.3, PLAN.md phase 4).

For every file of a corpus it runs Capriole (`cpdf check`, preferably the ASan build) and three
reference engines used strictly as external programs — qpdf, MuPDF (`mutool info`) and Poppler
(`pdfinfo`) — then compares page counts with the majority rule of the specification: when at least
two references agree on a page count, that is the expected answer.

Each tool runs in its own process with a timeout and a memory cap, from an empty working directory.
There is no network isolation (unprivileged user namespaces are not available on the development
machine): the tools involved do not use the network, and corpus files must never be opened with
anything else.

Outputs, under --report-dir/<name>-<timestamp>/:
  results.jsonl   one JSON object per file
  summary.md      counts, crashes, timeouts, mismatches, error kinds, unsupported features
and, when a previous run of the same --name exists, a regression section (files that used to match
the reference majority and no longer do).

Standard library only.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

MEMORY_CAP_BYTES = 4 * 1024**3  # reference tools; ASan's own limit is set through ASAN_OPTIONS
ASAN_OPTIONS = "hard_rss_limit_mb=4096:allocator_may_return_null=1:detect_leaks=0"


def run(cmd: list[str], timeout: float, env: dict[str, str] | None = None, cap_memory: bool = True) -> dict:
    """Runs cmd isolated from the caller; returns exit status, output tails and timing.

    The memory cap goes through prlimit(1), not preexec_fn, which is unsafe in a thread pool."""
    if cap_memory:
        cmd = ["prlimit", f"--as={MEMORY_CAP_BYTES}", "--", *cmd]
    with tempfile.TemporaryDirectory() as cwd:
        start = datetime.datetime.now()
        try:
            proc = subprocess.run(
                cmd,
                cwd=cwd,
                env=env,
                stdin=subprocess.DEVNULL,
                capture_output=True,
                timeout=timeout,
            )
            status = proc.returncode
            stdout = proc.stdout.decode("utf-8", "replace")
            stderr = proc.stderr.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            status, stdout, stderr = "timeout", "", ""
        seconds = (datetime.datetime.now() - start).total_seconds()
    return {"status": status, "stdout": stdout[-4000:], "stderr": stderr[-4000:], "seconds": round(seconds, 3)}


def int_after(pattern: str, text: str) -> int | None:
    match = re.search(pattern, text)
    return int(match.group(1)) if match else None


def run_capriole(cpdf: str, path: str, timeout: float) -> dict:
    env = {"PATH": "/usr/bin:/bin", "ASAN_OPTIONS": ASAN_OPTIONS, "UBSAN_OPTIONS": "print_stacktrace=1", "CAPRIOLE_PASSWORD": ""}
    result = run([cpdf, "check", path], timeout, env=env, cap_memory=False)
    out, err = result["stdout"], result["stderr"]
    sanitizer = re.search(r"(AddressSanitizer|UndefinedBehaviorSanitizer|runtime error)[^\n]*", err)
    if result["status"] == "timeout":
        outcome = "timeout"
    elif sanitizer or (isinstance(result["status"], int) and result["status"] < 0):
        outcome = "crash"
    elif result["status"] == 0:
        outcome = "ok"
    else:
        outcome = "error"
    first_error = next((line for line in (err + "\n" + out).splitlines() if line and "objects loaded" not in line and "not supported yet" not in line), "")
    return {
        "outcome": outcome,
        "pages": int_after(r": (\d+) pages,", out),
        "error": first_error[:300],
        "error_kind": (re.search(r"\((\w+)\)\s*$", first_error) or [None, None])[1] if outcome == "error" else None,
        "unsupported": [line.split(": ", 1)[-1] for line in out.splitlines() if "not supported yet" in line],
        "sanitizer": sanitizer.group(0)[:300] if sanitizer else None,
        "seconds": result["seconds"],
    }


def run_references(path: str, timeout: float) -> dict:
    qpdf_pages = run(["qpdf", "--show-npages", path], timeout)
    qpdf_check = run(["qpdf", "--check", path], timeout)
    mutool = run(["mutool", "info", path], timeout)
    pdfinfo = run(["pdfinfo", path], timeout)
    pages = {
        "qpdf": int_after(r"^(\d+)", qpdf_pages["stdout"]) if qpdf_pages["status"] in (0, 3) else None,
        "mutool": int_after(r"Pages:\s*(\d+)", mutool["stdout"]) if mutool["status"] == 0 else None,
        "pdfinfo": int_after(r"Pages:\s*(\d+)", pdfinfo["stdout"]) if pdfinfo["status"] == 0 else None,
    }
    counts = collections.Counter(value for value in pages.values() if value is not None)
    majority = next((value for value, n in counts.items() if n >= 2), None)
    qpdf_status = qpdf_check["status"]
    return {
        "pages": pages,
        "majority": majority,
        "unanimous": majority is not None and counts[majority] == 3,
        "qpdf_check": {0: "clean", 2: "errors", 3: "warnings"}.get(qpdf_status, str(qpdf_status)),
    }


def process(path: str, cpdf: str, timeout: float) -> dict:
    capriole = run_capriole(cpdf, path, timeout)
    reference = run_references(path, timeout)
    healthy = reference["qpdf_check"] == "clean" and reference["unanimous"]
    return {
        "file": path,
        "size": os.path.getsize(path),
        "capriole": capriole,
        "reference": reference,
        "healthy": healthy,
        "pages_match": reference["majority"] is not None and capriole["pages"] == reference["majority"],
    }


def collect(corpus: Path, limit: int | None) -> list[str]:
    # Absolute paths: every tool runs from its own empty working directory.
    files = sorted(str(p.resolve()) for p in corpus.rglob("*") if p.is_file() and not p.name.startswith("."))
    return files[:limit] if limit else files


def previous_results(report_dir: Path, name: str) -> dict[str, dict] | None:
    runs = sorted(p for p in report_dir.glob(f"{name}-*") if (p / "results.jsonl").exists())
    if not runs:
        return None
    with open(runs[-1] / "results.jsonl") as f:
        return {r["file"]: r for r in map(json.loads, f)}


def summarise(results: list[dict], previous: dict[str, dict] | None, args: argparse.Namespace) -> str:
    total = len(results)
    outcomes = collections.Counter(r["capriole"]["outcome"] for r in results)
    with_majority = [r for r in results if r["reference"]["majority"] is not None]
    healthy = [r for r in results if r["healthy"]]
    healthy_ok = [r for r in healthy if r["pages_match"] and r["capriole"]["outcome"] == "ok"]
    matched = [r for r in with_majority if r["pages_match"]]
    mismatched = [r for r in with_majority if r["capriole"]["pages"] is not None and not r["pages_match"]]
    missed = [r for r in with_majority if r["capriole"]["pages"] is None]
    beyond = [r for r in results if r["reference"]["majority"] is None and r["capriole"]["pages"] is not None]
    kinds = collections.Counter(r["capriole"]["error_kind"] for r in results if r["capriole"]["outcome"] == "error")
    unsupported = collections.Counter(re.sub(r"\d+", "N", u) for r in results for u in r["capriole"]["unsupported"])
    errors = collections.Counter(re.sub(r"\d+", "N", r["capriole"]["error"])[:120] for r in results if r["capriole"]["outcome"] == "error")

    def pct(n: int, d: int) -> str:
        return f"{n}/{d} ({100.0 * n / d:.2f}%)" if d else "0/0"

    lines = [
        f"# Corpus run: {args.name}",
        "",
        f"- date: {datetime.datetime.now().isoformat(timespec='seconds')}",
        f"- corpus: `{args.corpus}` — {total} files",
        f"- capriole: `{args.cpdf}`",
        "",
        "## Headline",
        "",
        f"- **crashes (sanitizer or signal): {outcomes['crash']}**",
        f"- timeouts ({args.timeout:g} s): {outcomes['timeout']}",
        f"- healthy files (qpdf --check clean, three references agree): {len(healthy)}; "
        f"**Capriole opens them with the same page count and no error: {pct(len(healthy_ok), len(healthy))}**",
        f"- files with a reference majority: {len(with_majority)}; same page count: {pct(len(matched), len(with_majority))}; "
        f"different count: {len(mismatched)}; no count from Capriole: {len(missed)}",
        f"- files no two references agree on, which Capriole reads: {len(beyond)}",
        "",
        "## Capriole outcomes",
        "",
        *[f"- {k}: {v}" for k, v in outcomes.most_common()],
        "",
        "## Error kinds",
        "",
        *[f"- {k}: {v}" for k, v in kinds.most_common()],
        "",
        "## Most frequent errors (numbers normalised)",
        "",
        *[f"- {v} × `{k}`" for k, v in errors.most_common(25)],
        "",
        "## Not supported yet",
        "",
        *[f"- {v} × {k}" for k, v in unsupported.most_common(15)],
        "",
    ]
    if outcomes["crash"]:
        lines += ["## Crashes", ""] + [f"- `{r['file']}`: {r['capriole']['sanitizer'] or r['capriole']['error']}" for r in results if r["capriole"]["outcome"] == "crash"] + [""]
    if outcomes["timeout"]:
        lines += ["## Timeouts", ""] + [f"- `{r['file']}`" for r in results if r["capriole"]["outcome"] == "timeout"] + [""]
    if mismatched:
        lines += ["## Page count differs from the reference majority", ""]
        lines += [f"- `{r['file']}`: capriole {r['capriole']['pages']}, references {r['reference']['pages']}" for r in mismatched[:50]] + [""]
    if healthy and len(healthy_ok) < len(healthy):
        lines += ["## Healthy files Capriole does not open cleanly", ""]
        lines += [f"- `{r['file']}`: {r['capriole']['outcome']} — {r['capriole']['error']}" for r in healthy if r not in healthy_ok][:50] + [""]
    if previous is not None:
        regressions = [r for r in results if r["file"] in previous and previous[r["file"]]["pages_match"] and not r["pages_match"]]
        new_crashes = [r for r in results if r["capriole"]["outcome"] == "crash" and previous.get(r["file"], {}).get("capriole", {}).get("outcome") != "crash"]
        fixed = [r for r in results if r["file"] in previous and not previous[r["file"]]["pages_match"] and r["pages_match"]]
        lines += ["## Compared with the previous run", "", f"- regressions (matched before, not now): {len(regressions)}", f"- new crashes: {len(new_crashes)}", f"- newly matching: {len(fixed)}", ""]
        lines += [f"- REGRESSION `{r['file']}`" for r in regressions[:50]]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("corpus", type=Path, help="directory with the corpus files (searched recursively)")
    parser.add_argument("--name", required=True, help="label of this corpus, used to compare with previous runs")
    parser.add_argument("--cpdf", default=str(Path(__file__).resolve().parent.parent / "build/asan/tools/cpdf/cpdf"))
    parser.add_argument("--report-dir", type=Path, default=Path(os.environ.get("CAPRIOLE_CORPUS_DIR", ".")) / "reports")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds per tool invocation")
    parser.add_argument("--limit", type=int, help="process only the first N files (sorted by path)")
    args = parser.parse_args()

    if not Path(args.cpdf).is_file():
        print(f"cpdf not found: {args.cpdf} (build the asan preset first)", file=sys.stderr)
        return 2
    files = collect(args.corpus, args.limit)
    if not files:
        print(f"no files under {args.corpus}", file=sys.stderr)
        return 2

    out_dir = args.report_dir / f"{args.name}-{datetime.datetime.now().strftime('%Y%m%d-%H%M%S')}"
    previous = previous_results(args.report_dir, args.name)
    out_dir.mkdir(parents=True)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool, open(out_dir / "results.jsonl", "w") as jsonl:
        futures = {pool.submit(process, f, args.cpdf, args.timeout): f for f in files}
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            result = future.result()
            results.append(result)
            jsonl.write(json.dumps(result) + "\n")
            if done % 50 == 0 or done == len(files):
                print(f"  {done}/{len(files)}", file=sys.stderr, flush=True)

    results.sort(key=lambda r: r["file"])
    summary = summarise(results, previous, args)
    (out_dir / "summary.md").write_text(summary)
    print(summary)
    print(f"report: {out_dir}", file=sys.stderr)
    crashes = sum(1 for r in results if r["capriole"]["outcome"] == "crash")
    return 1 if crashes else 0


if __name__ == "__main__":
    sys.exit(main())
