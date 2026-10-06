# Contributing to Capriole

Thank you for helping. A few rules keep the engine safe and the history useful.

## Before you start

- For anything larger than a small fix, open an issue first and describe what you want to change.
- Read [CLAUDE.md](CLAUDE.md) (the working rules, in Italian) and [docs/PLAN.md](docs/PLAN.md). The
  principles that are never broken: every byte of a file is hostile input; errors are values
  (`Result<T>`), not exceptions; the strict parser and the repair layer stay separate; an edit touches
  only what the user changed; no copyleft dependency is linked.

## Making a change

1. Branch from `development` (`feat/…`, `fix/…`); `main` only moves by merges from `development`.
2. Code and comments in English, following `.clang-format` and `.clang-tidy` (the gate checks them).
3. **Every fixed bug leaves a regression test** with its minimized file (`tests/regression/`).
4. A new parser comes with its fuzz target (`fuzz/`) and seed corpus.
5. A new dependency is first recorded in `cmake/Dependencies.cmake` (version + SHA-256) and in
   `docs/DEPENDENCIES.md`, with a licence compatible with 0BSD distribution; then regenerate
   `THIRD_PARTY_NOTICES.md` with `scripts/gen_notices.py`.
6. Run the gate: `scripts/check.sh` (with `SKIP_WASM=0 SKIP_E2E=0` when you touch the WebAssembly
   module or the apps). It must be green.
7. Commit messages follow [Conventional Commits](https://www.conventionalcommits.org).

Please do not attach confidential PDFs to issues or pull requests: a minimized file that reproduces the
problem is better, and it is what the regression test needs anyway.

## Developer Certificate of Origin

Contributions are accepted under the project's licence, [0BSD](LICENSE). Sign off each commit to certify
the [Developer Certificate of Origin](https://developercertificate.org) — that you wrote the change or
otherwise have the right to submit it under that licence:

```sh
git commit -s     # adds "Signed-off-by: Your Name <you@example.com>"
```

Pull requests with commits that are not signed off cannot be merged.
