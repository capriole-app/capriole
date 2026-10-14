// node --test deploy/reports/server.test.mjs   (deploy/web/test.sh runs it too)
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, readdirSync, existsSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { Quota, checked, dayFull, forget, limits, server } from './server.mjs';

const report = {
  v: 1,
  description: 'The page went blank after rotating it.',
  app: '0.1.0',
  engine: '0.0.1',
  platform: 'web',
  browser: 'Firefox 157',
  os: 'Linux',
  language: 'it',
  error: null,
  document: { pdfVersion: '1.7', pages: 3, encrypted: false, size: '<1MB', repairs: ['xref-rebuilt'] },
};

test('a report keeps only its expected fields, each cut to its size', () => {
  const kept = checked({
    ...report,
    description: 'x'.repeat(5000),
    name: 'contract-mario-rossi.pdf',
    browser: 'y'.repeat(500),
    document: { ...report.document, title: 'secret', pages: -1, size: '5MB', repairs: [1, 'a'.repeat(100)] },
  });
  assert.equal(kept.description.length, 2000);
  assert.equal(kept.browser.length, 64);
  assert.equal('name' in kept, false);
  assert.equal('title' in kept.document, false);
  assert.equal(kept.document.pages, null);
  assert.equal(kept.document.size, null);
  assert.deepEqual(kept.document.repairs, ['a'.repeat(64)]);
});

test('what is not a report is refused: no version, nothing said', () => {
  assert.equal(checked(null), null);
  assert.equal(checked({ ...report, v: 2 }), null);
  assert.equal(checked({ ...report, description: '  ', error: null }), null);
  assert.ok(checked({ ...report, description: '', error: 'the engine failed' }));
});

test('the quota: a few per sender, a cap for everyone, both forgotten after the window', () => {
  let now = 0;
  const quota = new Quota(() => now);
  for (let i = 0; i < limits.perSender; i++) assert.ok(quota.take('a'));
  assert.equal(quota.take('a'), false);
  assert.ok(quota.take('b'));
  now += limits.windowMs + 1;
  assert.ok(quota.take('a'));
  for (let i = 1; i < limits.everyone; i++) quota.take(`s${i}`);
  assert.equal(quota.take('someone-new'), false);
});

test('old days are deleted, the others kept', () => {
  const dir = mkdtempSync(join(tmpdir(), 'reports-'));
  mkdirSync(join(dir, '2025-01-01'));
  mkdirSync(join(dir, '2026-10-01'));
  forget(dir, new Date('2026-10-14T00:00:00Z'));
  assert.equal(existsSync(join(dir, '2025-01-01')), false);
  assert.equal(existsSync(join(dir, '2026-10-01')), true);
});

test('a day is full at its count or at its bytes, and the next day starts empty', () => {
  const now = new Date('2026-10-14T12:00:00Z');
  const byCount = mkdtempSync(join(tmpdir(), 'reports-'));
  assert.equal(dayFull(byCount, now), false); // no folder yet
  mkdirSync(join(byCount, '2026-10-14'));
  for (let i = 0; i < limits.perDay - 1; i++) writeFileSync(join(byCount, '2026-10-14', `${i}.json`), '{}');
  assert.equal(dayFull(byCount, now), false);
  writeFileSync(join(byCount, '2026-10-14', 'last.json'), '{}');
  assert.equal(dayFull(byCount, now), true);
  assert.equal(dayFull(byCount, new Date('2026-10-15T00:00:00Z')), false);

  const byBytes = mkdtempSync(join(tmpdir(), 'reports-'));
  mkdirSync(join(byBytes, '2026-10-14'));
  writeFileSync(join(byBytes, '2026-10-14', 'a.json'), 'x'.repeat(limits.dayBytes - 1));
  assert.equal(dayFull(byBytes, now), false);
  writeFileSync(join(byBytes, '2026-10-14', 'b.json'), 'x');
  assert.equal(dayFull(byBytes, now), true);
});

test('over HTTP: a full day refuses everyone, keeps nothing and uses up no one\'s quota', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'reports-'));
  const today = join(dir, new Date().toISOString().slice(0, 10));
  mkdirSync(today);
  writeFileSync(join(today, 'big.json'), 'x'.repeat(limits.dayBytes));
  const s = server(dir);
  await new Promise((resolve) => s.listen(0, '127.0.0.1', resolve));
  t.after(() => s.close());
  const post = () =>
    fetch(`http://127.0.0.1:${s.address().port}/api/report`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', 'x-forwarded-for': '203.0.113.9' },
      body: JSON.stringify(report),
    });
  for (let i = 0; i < limits.perSender + 2; i++) assert.equal((await post()).status, 429);
  assert.deepEqual(readdirSync(today), ['big.json']);
  // Room again (the next day, here by emptying today): the same sender's share is whole.
  rmSync(join(today, 'big.json'));
  for (let i = 0; i < limits.perSender; i++) assert.equal((await post()).status, 201);
});

test('over HTTP: kept as checked, with no address; refused when wrong, too large or too many', async (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'reports-'));
  const s = server(dir);
  await new Promise((resolve) => s.listen(0, '127.0.0.1', resolve));
  t.after(() => s.close());
  const url = `http://127.0.0.1:${s.address().port}/api/report`;
  const post = (body, headers = {}) =>
    fetch(url, {
      method: 'POST',
      headers: { 'content-type': 'application/json', 'x-forwarded-for': '203.0.113.7, 10.0.0.2', ...headers },
      body: typeof body === 'string' ? body : JSON.stringify(body),
    });

  const created = await post({ ...report, extra: 'dropped' });
  assert.equal(created.status, 201);
  const { id } = await created.json();
  const file = readFileSync(join(dir, `${id}.json`), 'utf8');
  assert.match(file, /went blank/);
  assert.doesNotMatch(file, /203\.0\.113|dropped|x-forwarded/);

  assert.equal((await post('not json')).status, 400);
  assert.equal((await post({ v: 1 })).status, 400);
  assert.equal((await post({ ...report, description: 'z'.repeat(limits.bytes) })).status, 413);
  assert.equal((await fetch(url)).status, 405);
  assert.equal((await post(report, { 'content-type': 'text/plain' })).status, 415);
  assert.equal((await fetch(url.replace('/api/report', '/other'))).status, 404);

  for (let i = 1; i < limits.perSender; i++) assert.equal((await post(report)).status, 201);
  assert.equal((await post(report)).status, 429);
  assert.equal((await post(report, { 'x-forwarded-for': '198.51.100.1' })).status, 201);
  const day = readdirSync(dir)[0];
  assert.equal(readdirSync(join(dir, day)).length, limits.perSender + 1);
});
