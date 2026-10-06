// A Vite plugin for the apps' «Licenze» page: at build time it writes licenses.json next to the app — Capriole's
// own licence (LICENSE), the notices of what is built into the engine (third_party/notices.json, from
// scripts/gen_notices.py), the JavaScript packages that ended up in the app's bundle (from the bundle itself:
// only code that ships), the resource pack's licences (static/resources/LICENSES) and, when given, the desktop
// shell's Rust crates (apps/desktop/crate-notices.json, from scripts/gen_crate_notices.py). And licenses.txt,
// the same as plain text.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repository = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');

// The shapes this plugin reads and writes (it does not import Vite's types: it lives outside the apps).
/** @typedef {{ name: string; version: string; license: string; text: string }} Package */
/**
 * @typedef {{
 *   capriole: string;
 *   engine: { name: string; version: string; license: string; use: string; note: string | null; text: string }[];
 *   npm: Package[];
 *   resources: { name: string; text: string }[];
 *   crates: { license: string; crates: string[]; text: string; source?: string[] }[];
 * }} Licenses
 */
/** @typedef {{ type: string; modules?: Record<string, { renderedLength: number }> }} Output */
/**
 * @typedef {{
 *   environment?: { name: string };
 *   emitFile(file: { type: 'asset'; fileName: string; source: string }): string;
 * }} Context
 */

/**
 * The package a module belongs to: its directory under the last node_modules of its path.
 * @param {string} id
 */
function packageDir(id) {
  const path = id
    .replace(/\\/g, '/')
    .replace(/[?#].*$/, '')
    .replace(/^\0/, '');
  const at = path.lastIndexOf('/node_modules/');
  if (at < 0) return null;
  const rest = path.slice(at + '/node_modules/'.length).split('/');
  const name = rest[0]?.startsWith('@') ? `${rest[0]}/${rest[1]}` : rest[0];
  return `${path.slice(0, at)}/node_modules/${name}`;
}

/** @param {string} dir */
function licenceText(dir) {
  const file = readdirSync(dir).find((f) => /^(licen[cs]e|copying)(\.|-|$)/i.test(f));
  return file ? readFileSync(join(dir, file), 'utf8').trim() : '';
}

/**
 * options.crates: include the desktop shell's crates. The app's directory (for static/resources/LICENSES)
 * is Vite's root.
 * @param {{ crates: boolean }} options
 */
export function licenses(options) {
  let app = '';
  /** @type {Map<string, Package>} */
  const packages = new Map();
  return {
    name: 'capriole-licenses',
    apply: /** @type {'build'} */ ('build'),
    /** @param {{ root: string }} config */
    configResolved(config) {
      app = config.root;
    },
    /**
     * @this {Context}
     * @param {unknown} _
     * @param {Record<string, Output>} bundle
     */
    generateBundle(_, bundle) {
      // Only the browser's build ships (SvelteKit also builds for the server, to prerender). The worker is
      // built apart and has no packages of its own (the engine client and the WebAssembly module are ours).
      if (this.environment && this.environment.name !== 'client') return;
      // The packages whose code is in the bundle.
      for (const chunk of Object.values(bundle)) {
        if (chunk.type !== 'chunk') continue;
        for (const [id, module] of Object.entries(chunk.modules ?? {})) {
          if (module.renderedLength === 0) continue;
          const dir = packageDir(id);
          if (!dir || packages.has(dir) || !existsSync(join(dir, 'package.json'))) continue;
          const manifest = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8'));
          packages.set(dir, {
            name: manifest.name,
            version: manifest.version,
            license: typeof manifest.license === 'string' ? manifest.license : (manifest.license?.type ?? ''),
            text: licenceText(dir),
          });
        }
      }
      const resources = join(app, 'static/resources/LICENSES');
      /** @type {Licenses} */
      const data = {
        capriole: readFileSync(join(repository, 'LICENSE'), 'utf8'),
        engine: JSON.parse(readFileSync(join(repository, 'third_party/notices.json'), 'utf8')),
        npm: [...packages.values()].sort((a, b) => a.name.localeCompare(b.name)),
        resources: existsSync(resources)
          ? readdirSync(resources)
              .sort()
              .map((name) => ({ name, text: readFileSync(join(resources, name), 'utf8') }))
          : [],
        crates: options.crates
          ? JSON.parse(readFileSync(join(repository, 'apps/desktop/crate-notices.json'), 'utf8'))
          : [],
      };
      this.emitFile({ type: 'asset', fileName: 'licenses.json', source: JSON.stringify(data) });
      this.emitFile({ type: 'asset', fileName: 'licenses.txt', source: plain(data) });
    },
  };
}

/** @param {Licenses} data */
function plain(data) {
  const out = ['Capriole — 0BSD', '', data.capriole, ''];
  const section = (/** @type {string} */ title) => out.push('', '='.repeat(78), title, '='.repeat(78), '');
  section('Built into the engine');
  for (const e of data.engine) {
    out.push(`--- ${e.name} ${e.version} (${e.license}) — ${e.use}`, '');
    if (e.note) out.push(e.note, '');
    if (e.text) out.push(e.text, '');
  }
  section('JavaScript packages in the app');
  for (const p of data.npm)
    out.push(`--- ${p.name} ${p.version} (${p.license})`, '', p.text || '(no licence file)', '');
  if (data.resources.length > 0) {
    section('Resource pack (fonts, CMaps, colour profile)');
    for (const r of data.resources) out.push(`--- ${r.name}`, '', r.text, '');
  }
  if (data.crates.length > 0) {
    section('Rust crates of the desktop shell');
    for (const c of data.crates) {
      out.push(`--- ${c.crates.join(', ')} (${c.license})`, '');
      if (c.source) out.push(`Source: ${c.source.join(' ')}`, '');
      out.push(c.text, '');
    }
  }
  return out.join('\n');
}
