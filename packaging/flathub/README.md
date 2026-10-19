# Capriole on Flathub

The manifest that publishes Capriole on Flathub (`app.capriole.Capriole`). It repackages the `.deb` of a
GitHub release and builds OpenSC for smart cards, with the PC/SC client library (pcsc-lite) the runtime
lacks. Flathub keeps the manifest in a repository of its own, `flathub/app.capriole.Capriole`, created when
the app is accepted; this folder is the reference copy.

At every release, after the GitHub release is published (docs/DEPLOY.md, «Rilascio di una versione»):

```sh
packaging/flathub/update.sh 0.1.0           # the .deb's URL and SHA-256, a <release> in the metainfo
flatpak-builder --user --install --force-clean build-dir packaging/flathub/app.capriole.Capriole.yml
flatpak run app.capriole.Capriole
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest packaging/flathub/app.capriole.Capriole.yml
```

Then a pull request to `flathub/app.capriole.Capriole`, with the three files. The first time it goes to
`flathub/flathub` instead, on the `new-pr` branch, following https://docs.flathub.org/docs/for-app-authors/submission.

Before the first submission:
- **Screenshots** in the metainfo (`<screenshots>`). Flathub requires them; they are PNGs at stable URLs,
  for example in this folder on `main`.
- The domain check: Flathub verifies `app.capriole` by a token served at
  `https://capriole.app/.well-known/org.flathub.VerifiedApps.txt`.
- Built and linted locally on 2026-10-18 (GNOME 51, which ships WebKitGTK 4.1: `ldd` finds every library;
  PC/SC reaches the host's pcscd 2.0.3 from the sandbox). At a new runtime, check WebKitGTK 4.1 again:
  `flatpak run --runtime=org.gnome.Platform//<n> --command=ldd app.capriole.Capriole /app/bin/capriole-desktop`.
