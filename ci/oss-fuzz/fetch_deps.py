#!/usr/bin/env python3
"""Downloads the archives cmake/Dependencies.cmake pins into a directory, checking each SHA-256: for builds
without network (OSS-Fuzz's Dockerfile runs it; then CMake gets -DCAPRIOLE_DEPS_DIR=<dir>).

    ci/oss-fuzz/fetch_deps.py <dir> [--tests]     # --tests: also GoogleTest
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def pinned(tests: bool) -> list[tuple[str, str]]:
    text = (ROOT / "cmake/Dependencies.cmake").read_text()
    pairs = re.findall(r"capriole_dependency_url\(_url (\S+)\)\s*\n\s*\w+\(\w+\s*\n\s*URL \$\{_url\}\s*\n\s*URL_HASH SHA256=([0-9a-f]{64})", text)
    if not pairs:
        sys.exit("fetch_deps: no pinned archive found in cmake/Dependencies.cmake")
    return [(url, sha) for url, sha in pairs if tests or "googletest" not in url]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dir")
    parser.add_argument("--tests", action="store_true")
    args = parser.parse_args()
    out = Path(args.dir)
    out.mkdir(parents=True, exist_ok=True)
    for url, sha in pinned(args.tests):
        target = out / url.rsplit("/", 1)[1]
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != sha:
            with urllib.request.urlopen(url, timeout=120) as response:  # noqa: S310 — pinned https URLs
                target.write_bytes(response.read())
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        if digest != sha:
            sys.exit(f"fetch_deps: {target.name}: SHA-256 {digest}, expected {sha}")
        print(f"{target.name} ok")


if __name__ == "__main__":
    main()
