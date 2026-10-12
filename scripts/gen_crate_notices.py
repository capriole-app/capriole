#!/usr/bin/env python3
"""The Rust crates the desktop shell (apps/desktop) is built from, with their licences, for the app's
«Licenze» page: apps/desktop/crate-notices.json (grouped by licence text — hundreds of crates share a few).

Only the crates linked into the binary for Linux (the desktop's first platform): normal dependencies of the
shell, followed from the lock file — not build scripts' or tests'. Read offline from cargo's registry
cache (cargo metadata), so the crates must have been fetched once (a build of apps/desktop does it).
Run again when apps/desktop/src-tauri/Cargo.lock changes; --check fails when the file is out of date.

    scripts/gen_crate_notices.py [--check] [--target x86_64-unknown-linux-gnu]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHELL = ROOT / "apps/desktop/src-tauri"
OUT = ROOT / "apps/desktop/crate-notices.json"
LICENSE_FILES = ("LICENSE", "LICENSE-MIT", "LICENSE-APACHE", "LICENSE.md", "LICENSE.txt", "COPYING", "LICENSE-MIT.md",
                 "LICENSE-APACHE.md", "LICENSE-ZLIB", "LICENSE-BSD", "UNLICENSE", "LICENSE-UNICODE")


def metadata(target: str) -> dict:
    result = subprocess.run(["cargo", "metadata", "--format-version", "1", "--locked", "--offline", "--filter-platform", target],
                            cwd=SHELL, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.exit(f"gen_crate_notices: cargo metadata failed (fetch the crates first: build apps/desktop):\n{result.stderr[-2000:]}")
    return json.loads(result.stdout)


def linked(meta: dict) -> list[dict]:
    """The packages reachable from the shell by normal dependencies (not build, not dev)."""
    packages = {p["id"]: p for p in meta["packages"]}
    nodes = {n["id"]: n for n in meta["resolve"]["nodes"]}
    root = meta["resolve"]["root"]
    seen = {root}
    stack = [root]
    while stack:
        for dep in nodes[stack.pop()]["deps"]:
            if any(k["kind"] is None for k in dep["dep_kinds"]) and dep["pkg"] not in seen:
                seen.add(dep["pkg"])
                stack.append(dep["pkg"])
    seen.discard(root)
    return sorted((packages[i] for i in seen), key=lambda p: (p["name"], p["version"]))


def texts(package: dict) -> str:
    folder = Path(package["manifest_path"]).parent
    found = []
    if package.get("license_file"):
        found.append(folder / package["license_file"])
    found += [folder / name for name in LICENSE_FILES if (folder / name).exists()]
    seen = []
    for path in found:
        if path.exists() and path not in seen:
            seen.append(path)
    return "\n\n".join(p.read_text(errors="replace").strip() for p in seen)


def notices(target: str) -> list[dict]:
    groups: dict[tuple[str, str], list[str]] = {}
    for p in linked(metadata(target)):
        spdx = p.get("license") or "see the licence text"
        text = texts(p)
        if not text:
            # No licence file in the crate: its SPDX expression is what it says.
            text = f"(The crate ships no licence file; its manifest declares: {spdx}.)"
        groups.setdefault((spdx, text), []).append(f"{p['name']} {p['version']}")
    out = []
    for (spdx, text), crates in sorted(groups.items(), key=lambda g: g[1][0]):
        item = {"license": spdx, "crates": crates, "text": text}
        if "MPL" in spdx:
            # MPL-2.0 (file-level copyleft): where the source of its files is, as the licence asks
            # (used unmodified, as published).
            item["source"] = [f"https://crates.io/crates/{c.split(' ')[0]}/{c.split(' ')[1]}" for c in crates]
        out.append(item)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--target", default="x86_64-unknown-linux-gnu")
    args = parser.parse_args()
    data = json.dumps(notices(args.target), ensure_ascii=False, indent=1) + "\n"
    if args.check:
        if not OUT.exists() or OUT.read_text() != data:
            sys.exit("gen_crate_notices: apps/desktop/crate-notices.json is out of date — run scripts/gen_crate_notices.py")
        print("crate notices up to date")
        return
    OUT.write_text(data)
    groups = json.loads(data)
    print(f"wrote {OUT.relative_to(ROOT)}: {sum(len(g['crates']) for g in groups)} crates, {len(groups)} licence texts")


if __name__ == "__main__":
    main()
