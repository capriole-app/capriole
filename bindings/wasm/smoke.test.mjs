// Smoke test for the built WebAssembly module: loads it under Node and calls the C API.
// Usage: node smoke.test.mjs <path/to/capriole.mjs>

import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';

const modulePath = process.argv[2];
assert.ok(modulePath, 'usage: node smoke.test.mjs <path/to/capriole.mjs>');

const { default: createCapriole } = await import(pathToFileURL(modulePath).href);
const m = await createCapriole();

assert.match(m.UTF8ToString(m._capriole_version()), /^\d+\.\d+\.\d+$/);

function probe(text) {
  const bytes = new TextEncoder().encode(text);
  const data = m._malloc(bytes.length);
  const out = m._malloc(8);
  try {
    m.HEAPU8.set(bytes, data);
    const status = m._capriole_probe_header(data, bytes.length, out, out + 4);
    return { status, major: m.HEAP32[out >> 2], minor: m.HEAP32[(out >> 2) + 1] };
  } finally {
    m._free(data);
    m._free(out);
  }
}

assert.deepEqual(probe('%PDF-1.7\n%\xE2\xE3\n'), { status: 0, major: 1, minor: 7 });
assert.equal(probe('not a pdf').status, 4 /* CAPRIOLE_ERROR_SYNTAX */);

// Documents: open a small PDF, read its summary and an object as JSON, save it.
function withBytes(bytes, fn) {
  const ptr = m._malloc(Math.max(bytes.length, 1));
  try {
    m.HEAPU8.set(bytes, ptr);
    return fn(ptr);
  } finally {
    m._free(ptr);
  }
}

function takeJson(outPtr) {
  const json = m.HEAPU32[outPtr >> 2];
  const text = m.UTF8ToString(json);
  m._capriole_free(json);
  return JSON.parse(text);
}

const offsets = [];
let pdf = '%PDF-1.7\n';
for (const body of ['<</Type/Catalog/Pages 2 0 R>>', '<</Type/Pages/Kids[3 0 R]/Count 1>>', '<</Type/Page/Parent 2 0 R>>']) {
  offsets.push(pdf.length);
  pdf += `${offsets.length} 0 obj\n${body}\nendobj\n`;
}
const xref = pdf.length;
pdf += `xref\n0 4\n0000000000 65535 f \n${offsets.map((o) => `${String(o).padStart(10, '0')} 00000 n \n`).join('')}`;
pdf += `trailer\n<</Size 4/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;

const out = m._malloc(8);
try {
  const bytes = new TextEncoder().encode(pdf);
  assert.equal(withBytes(bytes, (ptr) => m._capriole_document_open(ptr, bytes.length, 0, 1, out)), 0, m.UTF8ToString(m._capriole_last_error_message()));
  const doc = m.HEAPU32[out >> 2];

  assert.equal(m._capriole_document_summary_json(doc, out), 0);
  const summary = takeJson(out);
  assert.equal(summary.version, '1.7');
  assert.deepEqual(summary.pages, [[3, 0]]);
  assert.deepEqual(summary.repairs, []);

  assert.equal(m._capriole_document_object_json(doc, 3, out), 0);
  const page = takeJson(out);
  assert.equal(page.object.t, 'dict');
  assert.deepEqual(page.object.v[0], ['Type', { t: 'name', v: 'Page' }]);

  // The page has no content: no text, and its size (the default Letter page).
  assert.equal(m._capriole_document_page_text_json(doc, 0, 0, out), 0, m.UTF8ToString(m._capriole_last_error_message()));
  const text = takeJson(out);
  assert.equal(text.text, '');
  assert.deepEqual(text.chars, []);
  assert.equal(text.order, 'content');

  // Nor a model with anything in it.
  assert.equal(m._capriole_document_page_model_json(doc, 0, out), 0, m.UTF8ToString(m._capriole_last_error_message()));
  const model = takeJson(out);
  assert.deepEqual(model.elements, []);
  assert.deepEqual(model.paragraphs, []);
  // Nothing to edit there.
  const spec = new TextEncoder().encode('{"element":0,"delete":true}\0');
  assert.equal(withBytes(spec, (ptr) => m._capriole_document_page_object_edit(doc, 0, ptr, out)), 2 /* CAPRIOLE_ERROR_OUT_OF_RANGE */);

  assert.equal(m._capriole_document_save(doc, 0, 0, out, out + 4), 0);
  const saved = m.HEAPU32[out >> 2];
  const size = m.HEAPU32[(out >> 2) + 1];
  assert.equal(new TextDecoder().decode(m.HEAPU8.subarray(saved, saved + 8)), '%PDF-1.7');
  assert.ok(size > pdf.length / 2);
  m._capriole_free(saved);

  m._capriole_document_close(doc);
} finally {
  m._free(out);
}

console.log('wasm smoke test: ok');
