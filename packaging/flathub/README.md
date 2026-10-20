# Capriole on Flathub — deferred

The Flatpak manifest of Capriole (`app.capriole.Capriole`). It repackages the `.deb` of a GitHub release and
builds OpenSC for smart cards, with the PC/SC client library (pcsc-lite) the runtime lacks.

**The Flathub submission is deferred** (2026-10-20). Flathub's requirements
(https://docs.flathub.org/docs/for-app-authors/requirements) rule it out as it stands:
- **Generative AI policy**: Flathub manifests must not contain AI-generated or AI-assisted content (disclosing
  it does not exempt them), and AI tools or agents must not open or automate the submission pull request or
  its review. This manifest and its metainfo were written with an AI assistant: a submission needs a
  manifest and a pull request written by hand, and the AI-generated parts of the app disclosed.
- **Built from source**: an open-source application must be built entirely from source; repackaging the
  release's `.deb` is not allowed. A source build means emsdk, cargo and npm, offline, inside flatpak-builder.
- **Development history**: a sustained public history, real-world use and standard release practices; very
  young applications are generally refused (the repository is public since 2026-10-14, 0.1.0 is of 2026-10-18).
- The pull request asks for a video of the app running on Linux as a Flatpak.

Meanwhile Linux is served by the `.deb` and the AppImage of the GitHub release. This folder stays as a
working reference: the manifest builds and passes Flathub's linter (`manifest` and `repo`, below).

To build it for a release, after the GitHub release is published:

```sh
packaging/flathub/update.sh 0.1.0           # the .deb's URL and SHA-256, a <release> in the metainfo
flatpak-builder --user --install --force-clean build-dir packaging/flathub/app.capriole.Capriole.yml
flatpak run app.capriole.Capriole
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest packaging/flathub/app.capriole.Capriole.yml
```

For a repository like Flathub's, with the screenshots mirrored, and the linter on it (without
`--compose-url-policy=full` the linter reports the screenshots and the icon as not mirrored):

```sh
flatpak run org.flatpak.Builder --force-clean --sandbox --user --install-deps-from=flathub --ccache \
  --compose-url-policy=full --mirror-screenshots-url=https://dl.flathub.org/media/ --repo=repo \
  build-dir packaging/flathub/app.capriole.Capriole.yml
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
```

- **Screenshots**: `screenshots/`, from the desktop app (on documents of our own, with no name in them).
  The metainfo points at each at the commit that added it, so the URLs never change; a new screenshot is a
  new commit, then its URL in the metainfo.
- The domain check, at submission: Flathub verifies `app.capriole` by a token served at
  `https://capriole.app/.well-known/org.flathub.VerifiedApps.txt`.
- Built locally and linted clean on 2026-10-19 (GNOME 51, which ships WebKitGTK 4.1: `ldd` finds every library;
  PC/SC reaches the host's pcscd 2.0.3 from the sandbox). At a new runtime, check WebKitGTK 4.1 again:
  `flatpak run --runtime=org.gnome.Platform//<n> --command=ldd app.capriole.Capriole /app/bin/capriole-desktop`.
