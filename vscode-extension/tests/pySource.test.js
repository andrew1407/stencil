// Which Python buffers are Stencil's: a .pystc always, a .py only where the `# @use stencil`
// marker stands on a comment line of its own. The twin of jsMarker.test.js, one layer down:
// pySource takes documents, so no editor is needed.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const pySource = createRequire(import.meta.url)('../src/lib/pySource.js');
const ids = createRequire(import.meta.url)('../src/lib/ids.js');

// A fresh uri per buffer: the marker scan is cached per (document, version).
let made = 0;
const doc = (languageId, text) => ({
  languageId, version: 1, uri: `file:///tmp/a${(made += 1)}`, getText: () => text,
});
const py = (text) => doc('python', text);
const pystc = (text) => doc(ids.PY_LANGUAGE_ID, text);

test('a .pystc is Stencil’s with or without the marker', () => {
  assert.ok(pySource.isPySource(pystc('editor.crop("5%")')));
  assert.ok(pySource.isPySource(pystc(`${ids.PY_USE_MARKER}\n`)));
  assert.ok(pySource.isPyDocument(pystc('')));
});

test('a plain .py joins in by saying the marker, anywhere, on its own line', () => {
  assert.ok(!pySource.isPySource(py('import sys\n')));
  assert.ok(pySource.isPySource(py(`${ids.PY_USE_MARKER}\nimport sys\n`)));
  assert.ok(pySource.isPySource(py(`import sys\n\n  ${ids.PY_USE_MARKER}\n`)));
  assert.equal(pySource.markerLine(py(`import sys\n${ids.PY_USE_MARKER}\n`)), 1);
});

test('words with code in front of them are prose, not the marker', () => {
  assert.ok(!pySource.isPySource(py(`import sys  ${ids.PY_USE_MARKER}\n`)));
  assert.equal(pySource.markerSpan(`x = 1  ${ids.PY_USE_MARKER}`), null);
  // The span covers `@use stencil`, never the `#` that opened the comment.
  assert.deepEqual(pySource.markerSpan(`  ${ids.PY_USE_MARKER}`), { start: 4, end: 16 });
});

test('a buffer in another language is nobody else’s Python', () => {
  assert.ok(!pySource.isPyDocument(doc('javascript', ids.PY_USE_MARKER)));
  assert.ok(!pySource.isPySource(doc('stencil-script', ids.PY_USE_MARKER)));
  assert.ok(!pySource.isPySource(null));
});
