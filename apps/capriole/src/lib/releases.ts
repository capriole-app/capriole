// The desktop app's installers, from the latest GitHub release of the repository (scripts/build_release.sh
// names them without a version, so these links never change). Plain links: the page makes no request of its
// own (its CSP allows none to GitHub). The download dialog and the landing's desktop section.

export const repository = 'https://github.com/capriole-app/capriole';
export const releases = `${repository}/releases`;

/** A file of the latest release. */
export const latest = (file: string) => `${releases}/latest/download/${file}`;

export const installers = {
  windows: 'Capriole_x64-setup.exe',
  deb: 'Capriole_amd64.deb',
  appImage: 'Capriole_amd64.AppImage',
} as const;
