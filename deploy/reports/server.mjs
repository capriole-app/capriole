// capriole-reports: receives the problem reports the web app sends («Segnala un problema»), each one
// sent by a person who has seen all of it first. Every request is hostile until proven otherwise: a
// report is at most 16 KB, only the expected fields are kept (anything else is dropped, every string is
// cut to its size), and the reports a sender — or everyone — can make in an hour are capped. The address
// that cap is counted by lives only in memory and is never written: a report on disk has no address, no
// header, nothing but what the person was shown. Kept twelve months (the terms of use), then deleted.
//
// No dependency: Node's own modules. Reached only through the web app's Caddy (deploy/web/Caddyfile),
// on the compose's inner network.
//
//   node server.mjs                     PORT (8081), REPORTS_DIR (/reports)
import { randomBytes } from 'node:crypto';
import { mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { join } from 'node:path';

export const limits = {
  bytes: 16 * 1024,
  perSender: 5,
  everyone: 200,
  windowMs: 60 * 60 * 1000,
  keepDays: 365,
};

const locales = new Set(['it', 'en', 'fr', 'de', 'es']);
const sizes = new Set(['<1MB', '1-10MB', '10-100MB', '>100MB']);

const text = (v, max) => (typeof v === 'string' ? v.slice(0, max) : null);

/** The report as kept: only the expected fields, each checked; null when it is not a report at all. */
export function checked(raw) {
  if (typeof raw !== 'object' || raw === null || raw.v !== 1) return null;
  const description = text(raw.description, 2000)?.trim() ?? '';
  const error = text(raw.error, 500);
  if (description === '' && !error) return null;
  const d = raw.document;
  const document =
    typeof d === 'object' && d !== null
      ? {
          pdfVersion: text(d.pdfVersion, 8),
          pages: Number.isInteger(d.pages) && d.pages >= 0 && d.pages <= 1_000_000 ? d.pages : null,
          encrypted: typeof d.encrypted === 'boolean' ? d.encrypted : null,
          size: sizes.has(d.size) ? d.size : null,
          repairs: Array.isArray(d.repairs)
            ? d.repairs.slice(0, 50).flatMap((r) => (typeof r === 'string' ? [r.slice(0, 64)] : []))
            : [],
        }
      : null;
  return {
    v: 1,
    description,
    app: text(raw.app, 32),
    engine: text(raw.engine, 32),
    platform: raw.platform === 'web' || raw.platform === 'desktop' ? raw.platform : null,
    browser: text(raw.browser, 64),
    os: text(raw.os, 64),
    language: locales.has(raw.language) ? raw.language : null,
    error,
    document,
  };
}

/** How many reports, in the last window, each sender and everyone made. */
export class Quota {
  #senders = new Map();
  #everyone = [];
  constructor(now = Date.now) {
    this.now = now;
  }
  #recent(times) {
    const since = this.now() - limits.windowMs;
    return times.filter((t) => t > since);
  }
  /** One more from this sender, if the caps allow it. */
  take(sender) {
    this.#everyone = this.#recent(this.#everyone);
    const mine = this.#recent(this.#senders.get(sender) ?? []);
    if (mine.length >= limits.perSender || this.#everyone.length >= limits.everyone) {
      this.#senders.set(sender, mine);
      return false;
    }
    mine.push(this.now());
    this.#everyone.push(this.now());
    this.#senders.set(sender, mine);
    // The map forgets whoever has nothing left in the window: it never grows into a list of addresses.
    for (const [key, times] of this.#senders) if (this.#recent(times).length === 0) this.#senders.delete(key);
    return true;
  }
}

/** The client, as the proxies in front tell it (the gateway, then the web app's Caddy): the first one. */
function sender(request) {
  const forwarded = request.headers['x-forwarded-for'];
  const first = typeof forwarded === 'string' ? forwarded.split(',')[0].trim() : '';
  return first || request.socket.remoteAddress || 'unknown';
}

/** Writes a report in its day's folder; returns its name. */
export function keep(dir, report, now = new Date()) {
  const day = now.toISOString().slice(0, 10);
  const id = `${now.toISOString().slice(11, 19).replaceAll(':', '')}-${randomBytes(4).toString('hex')}`;
  mkdirSync(join(dir, day), { recursive: true });
  writeFileSync(join(dir, day, `${id}.json`), `${JSON.stringify({ received: now.toISOString(), ...report }, null, 2)}\n`, {
    flag: 'wx',
  });
  return `${day}/${id}`;
}

/** Deletes the days older than the time reports are kept. */
export function forget(dir, now = new Date()) {
  const oldest = new Date(now.getTime() - limits.keepDays * 24 * 60 * 60 * 1000).toISOString().slice(0, 10);
  let entries = [];
  try {
    entries = readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const e of entries) {
    if (e.isDirectory() && /^\d{4}-\d{2}-\d{2}$/.test(e.name) && e.name < oldest) {
      rmSync(join(dir, e.name), { recursive: true, force: true });
    }
  }
}

function answer(response, status, body) {
  response.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  response.end(JSON.stringify(body));
}

export function server(dir, quota = new Quota()) {
  return createServer((request, response) => {
    if (request.url === '/healthz' && request.method === 'GET') return answer(response, 200, { ok: true });
    if (request.url !== '/api/report') return answer(response, 404, { error: 'not found' });
    if (request.method !== 'POST') return answer(response, 405, { error: 'POST only' });
    if (!(request.headers['content-type'] ?? '').startsWith('application/json')) {
      return answer(response, 415, { error: 'JSON only' });
    }
    if (Number(request.headers['content-length'] ?? 0) > limits.bytes) {
      return answer(response, 413, { error: 'too large' });
    }
    const chunks = [];
    let size = 0;
    let refused = false;
    request.on('data', (chunk) => {
      size += chunk.length;
      if (size > limits.bytes && !refused) {
        refused = true;
        answer(response, 413, { error: 'too large' });
        request.destroy();
      } else if (!refused) chunks.push(chunk);
    });
    request.on('end', () => {
      if (refused) return;
      let report;
      try {
        report = checked(JSON.parse(Buffer.concat(chunks).toString('utf8')));
      } catch {
        report = null;
      }
      if (!report) return answer(response, 400, { error: 'not a report' });
      if (!quota.take(sender(request))) return answer(response, 429, { error: 'too many reports' });
      try {
        return answer(response, 201, { id: keep(dir, report) });
      } catch (error) {
        console.error('cannot keep a report:', error);
        return answer(response, 500, { error: 'not kept' });
      }
    });
  });
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const dir = process.env.REPORTS_DIR ?? '/reports';
  const port = Number(process.env.PORT ?? 8081);
  forget(dir);
  setInterval(() => forget(dir), 24 * 60 * 60 * 1000).unref();
  const s = server(dir);
  s.requestTimeout = 10_000;
  s.headersTimeout = 5_000;
  s.listen(port, () => console.log(`capriole-reports on :${port}, in ${dir}`));
  for (const signal of ['SIGTERM', 'SIGINT']) process.on(signal, () => s.close(() => process.exit(0)));
}
