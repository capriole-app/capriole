// The Content-Security-Policy of the web app as served (deploy/web): its inline scripts — the theme's,
// applied before the first paint (src/app.html), and SvelteKit's start — allowed by their hashes,
// read from the built pages, so that a hash can never be stale. The engine needs 'wasm-unsafe-eval'
// (WebAssembly) and its worker; nothing is fetched from elsewhere.
//
//   node deploy/web/csp.mjs <build dir>   → prints the Caddy line (header Content-Security-Policy "…")
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const root = process.argv[2];
if (!root) {
  console.error('usage: node deploy/web/csp.mjs <build dir>');
  process.exit(2);
}

function pages(dir) {
  return readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? pages(join(dir, e.name)) : e.name.endsWith('.html') ? [join(dir, e.name)] : [],
  );
}

const hashes = new Set();
for (const page of pages(root)) {
  const html = readFileSync(page, 'utf8');
  for (const [, attributes, body] of html.matchAll(/<script(\s[^>]*)?>([\s\S]*?)<\/script>/g)) {
    if (/\ssrc\s*=/.test(attributes ?? '') || body.trim() === '') continue;
    hashes.add(`'sha256-${createHash('sha256').update(body).digest('base64')}'`);
  }
}
if (hashes.size === 0) {
  console.error(`no inline script in ${root}: is it the built app?`);
  process.exit(1);
}

const policy = [
  "default-src 'self'",
  `script-src 'self' 'wasm-unsafe-eval' ${[...hashes].sort().join(' ')}`,
  "worker-src 'self' blob:",
  "connect-src 'self' blob: data:",
  "img-src 'self' blob: data:",
  "font-src 'self' blob: data:",
  "style-src 'self' 'unsafe-inline'",
  "object-src 'none'",
  "base-uri 'self'",
  "form-action 'none'",
  "frame-ancestors 'none'",
].join('; ');
console.log(`header Content-Security-Policy "${policy}"`);
