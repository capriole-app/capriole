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

console.log('wasm smoke test: ok');
