// The web app the shell carries (tauri.conf.json, frontendDist): apps/capriole/build copied into frontend/,
// without what only the web's landing and search engines use — the screenshots of its sections
// (landing/: those sections are not shown in the shell, app.css data-web-only), the Open Graph images,
// the sitemap and robots.txt. 2 MB less in each installer. Run after the web app's build
// (beforeBuildCommand; scripts/check.sh before cargo clippy, which compiles it in).
import { cpSync, existsSync, rmSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const from = fileURLToPath(new URL('../../capriole/build', import.meta.url));
const to = fileURLToPath(new URL('../frontend', import.meta.url));
const webOnly = new Set(['landing', 'og', 'sitemap.xml', 'robots.txt']);

if (!existsSync(`${from}/index.html`)) {
  console.error(`${from}: the web app is not built (npm --prefix apps/capriole run build)`);
  process.exit(1);
}
rmSync(to, { recursive: true, force: true });
cpSync(from, to, {
  recursive: true,
  filter: (path) => path === from || !webOnly.has(path.slice(from.length + 1)),
});
