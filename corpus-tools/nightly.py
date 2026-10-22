#!/usr/bin/env python3
"""The nightly corpus run (Jenkins job capriole-corpus, ci/corpus.Jenkinsfile).

Every night, one zip of SafeDocs (CC-MAIN-2021-31-PDF-UNTRUNCATED, 0001-7932) and one of UNSAFE
(CC-MAIN-2021-31-UNSAFE corpora-pdf-*), chosen from the date: the day's number modulo the count, so
consecutive nights take consecutive zips, every zip comes round in time, and nothing has to be
remembered between runs. They are downloaded into --work, run through run_corpus.py with --roundtrip,
and deleted. On Mondays (or with --fixed) the three fixed corpora too — safedocs/0000, unsafe/x002,
unsafe/x008, kept in $CAPRIOLE_CORPUS_DIR — compared with their previous run: these are the ones whose
regressions mean something, since a nightly zip is seen again only after years.

Each run's summary.md and results.jsonl.gz go to --artifacts/<name>/, with an index (summary.md) of the
headlines. Exit status: 1 a crash in any run; 3 a regression against a previous run (and nothing worse);
2 a run that could not be made (download, usage); 0 otherwise.

Standard library only. The corpus files are hostile: only run_corpus.py opens them.
"""

from __future__ import annotations

import argparse
import datetime
import gzip
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUCKET = "https://digitalcorpora.s3.amazonaws.com/"
UNSAFE_PREFIX = "corpora/files/CC-MAIN-2021-31-UNSAFE/corpora-pdf/"
SAFEDOCS_LAST = 7932  # zipfiles/7000-7999/7932.zip is the last one
FIXED = [("safedocs-0000", "safedocs/0000"), ("unsafe-x002", "unsafe/x002"), ("unsafe-x008", "unsafe/x008")]
EXIT_CRASH, EXIT_FAILED, EXIT_REGRESSION = 1, 2, 3


def unsafe_names() -> list[str]:
    """The UNSAFE zips, from the bucket's listing (1,000 keys a page): x000-x021, x022-00…x031-04,
    x032-x999 at the time of writing; listed, not assumed."""
    names: list[str] = []
    token = None
    while True:
        query = {"list-type": "2", "prefix": UNSAFE_PREFIX, **({"continuation-token": token} if token else {})}
        for attempt in range(3):
            try:
                with urllib.request.urlopen(BUCKET + "?" + urllib.parse.urlencode(query), timeout=60) as response:
                    page = response.read().decode()
                break
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(30)
        names += re.findall(r"<Key>" + re.escape(UNSAFE_PREFIX) + r"corpora-pdf-(x\d{3}(?:-\d{2})?)\.zip</Key>", page)
        match = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", page)
        if not match:
            return sorted(names)
        token = match.group(1)


def choose(day: datetime.date, unsafe: list[str]) -> tuple[str, str]:
    """The night's two zips. The fixed corpora are left out: they have their own run."""
    fixed = {path.split("/")[1] for _, path in FIXED}
    candidates = [name for name in unsafe if name not in fixed]
    n = day.toordinal()
    return f"{1 + n % SAFEDOCS_LAST:04d}", candidates[n % len(candidates)]


def latest_run(report_dir: Path, name: str) -> Path | None:
    runs = sorted(p for p in report_dir.glob(f"{name}-*") if (p / "summary.md").exists() and re.fullmatch(rf"{re.escape(name)}-\d{{8}}-\d{{6}}", p.name))
    return runs[-1] if runs else None


def headline(summary: str) -> list[str]:
    section = summary.split("## Headline", 1)[-1].split("\n## ", 1)[0]
    return [line for line in section.splitlines() if line.startswith("- ")]


def run_one(name: str, corpus: Path, args: argparse.Namespace) -> tuple[int, list[str]]:
    """run_corpus.py on one corpus; its report copied to the artifacts. Returns (status, headline)."""
    print(f"\n==> {name}: {corpus}", flush=True)
    started = time.time()
    status = subprocess.run(
        [sys.executable, "-I", str(HERE / "run_corpus.py"), str(corpus), "--name", name, "--cpdf", args.cpdf,
         "--report-dir", str(args.report_dir), "--jobs", str(args.jobs), "--roundtrip", "--fail-on-regressions",
         *(["--limit", str(args.limit)] if args.limit else [])],
        stdout=subprocess.DEVNULL,  # the summary is in the report; progress goes to stderr
    ).returncode
    minutes = (time.time() - started) / 60
    report = latest_run(args.report_dir, name)
    if report is None or report.stat().st_mtime < started:
        return (status or EXIT_FAILED), [f"- no report (exit {status})"]
    target = args.artifacts / name
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy(report / "summary.md", target / "summary.md")
    with open(report / "results.jsonl", "rb") as src, gzip.open(target / "results.jsonl.gz", "wb") as dst:
        shutil.copyfileobj(src, dst)
    lines = headline((report / "summary.md").read_text())
    regressions = re.search(r"regressions \(matched before, not now\): (\d+)", (report / "summary.md").read_text())
    if regressions:
        lines.append(f"- regressions against the previous run: {regressions.group(1)}")
    lines.append(f"- {minutes:.0f} min, exit {status}, report `{report.name}`")
    return status, lines


def fetch(into: Path, *selection: str) -> bool:
    return subprocess.run([str(HERE / "fetch_corpus.sh"), "--into", str(into), *selection]).returncode == 0


def main() -> int:
    corpus_dir = Path(os.environ.get("CAPRIOLE_CORPUS_DIR", HERE.parent.parent / "CORPORA"))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--date", type=datetime.date.fromisoformat, default=datetime.date.today(), help="the night to reproduce (YYYY-MM-DD)")
    parser.add_argument("--cpdf", default=str(HERE.parent / "build/corpus/tools/cpdf/cpdf"))
    parser.add_argument("--work", type=Path, default=Path("corpus-work"), help="where the night's zips go (deleted after)")
    parser.add_argument("--report-dir", type=Path, default=corpus_dir / "reports")
    parser.add_argument("--artifacts", type=Path, default=Path("corpus-reports"))
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--fixed", action="store_true", help="run the fixed corpora even if it is not Monday")
    parser.add_argument("--dry-run", action="store_true", help="only say what this night would run")
    parser.add_argument("--limit", type=int, help="only the first N files of each corpus (to try the chain)")
    args = parser.parse_args()

    safedocs, unsafe = choose(args.date, unsafe_names())
    fixed = args.fixed or args.date.weekday() == 0
    print(f"{args.date}: safedocs/{safedocs}, unsafe/{unsafe}" + (", and the fixed corpora" if fixed else ""), flush=True)
    if args.dry_run:
        return 0
    if not Path(args.cpdf).is_file():
        print(f"cpdf not found: {args.cpdf} (cmake --preset corpus && cmake --build --preset corpus)", file=sys.stderr)
        return EXIT_FAILED

    args.artifacts.mkdir(parents=True, exist_ok=True)
    results: list[tuple[str, int, list[str]]] = []
    try:
        for name, selection, subdir in ((f"nightly-safedocs-{safedocs}", ["--safedocs", safedocs], f"safedocs/{safedocs}"),
                                        (f"nightly-unsafe-{unsafe}", ["--unsafe", unsafe], f"unsafe/{unsafe}")):
            if not fetch(args.work, *selection):
                results.append((name, EXIT_FAILED, ["- download failed"]))
                continue
            try:
                results.append((name, *run_one(name, args.work / subdir, args)))
            finally:
                # Each zip extracted is up to ~4 GB: gone before the next one comes.
                shutil.rmtree(args.work, ignore_errors=True)
        if fixed:
            if fetch(corpus_dir):  # cached: downloaded once, on the first Monday
                for name, subdir in FIXED:
                    results.append((name, *run_one(name, corpus_dir / subdir, args)))
            else:
                results.append(("fixed corpora", EXIT_FAILED, ["- download failed"]))
    finally:
        shutil.rmtree(args.work, ignore_errors=True)

    index = [f"# Corpus night {args.date}", ""]
    for name, status, lines in results:
        index += [f"## {name}", "", *lines, ""]
    (args.artifacts / "summary.md").write_text("\n".join(index))
    print("\n".join(index))

    statuses = [status for _, status, _ in results]
    if EXIT_CRASH in statuses:
        return EXIT_CRASH
    if any(status not in (0, EXIT_REGRESSION) for status in statuses):
        return EXIT_FAILED
    return EXIT_REGRESSION if EXIT_REGRESSION in statuses else 0


if __name__ == "__main__":
    sys.exit(main())
