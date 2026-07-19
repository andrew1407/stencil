// Unit tests for the pure .stencil project-file (de)serializer (js/core/projectFile.js).
// node --test never touches the DOM/wasm — this exercises the JS reference directly.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  buildProjectFile,
  serializeProjectFile,
  parseProjectFile,
  STENCIL_FILE_FORMAT,
  STENCIL_FILE_VERSION,
} from '../js/core/projectFile.js';

// A real 1×1 red PNG (data-URL) — small enough to inline, valid enough to decode.
const RED_1x1 =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mO4Y2T0HwAFbgJAIh+PxAAAAABJRU5ErkJggg==';

const minimalDoc = () => ({
  format: STENCIL_FILE_FORMAT,
  version: 1,
  name: 'red dot',
  image: { dataUrl: RED_1x1, ext: 'png', w: 1, h: 1 },
  layout: {
    imageWidth: 1,
    imageHeight: 1,
    lines: [{
      points: [{ x: 0, y: 0 }, { x: 1, y: 1 }],
      color: '#ff0000', thickness: 2, markerSize: 4, style: 'solid', locked: false, fillColor: 'transparent',
    }],
    imageFilter: 'none',
    rotationQuarters: 0,
  },
});

test('parse: accepts a minimal valid document', () => {
  const res = parseProjectFile(minimalDoc());
  assert.equal(res.ok, true);
  assert.equal(res.project.name, 'red dot');
  assert.equal(res.project.image.w, 1);
  assert.equal(res.project.image.h, 1);
  assert.equal(res.project.image.ext, 'png');
  assert.equal(res.project.layout.lines.length, 1);
  assert.equal(res.project.layout.imageFilter, 'none');
  assert.equal(res.project.theme, null);
});

test('parse: accepts JSON text as well as an object', () => {
  const res = parseProjectFile(JSON.stringify(minimalDoc()));
  assert.equal(res.ok, true);
  assert.equal(res.project.layout.lines.length, 1);
});

test('round-trip: build → serialize → parse is stable', () => {
  const parsed = parseProjectFile(minimalDoc()).project;
  const state = {
    name: parsed.name,
    color: '#7c3aed',
    keywords: ['a', 'b'],
    source: 'https://ex/a.png',
    resource: 'https://ex/p',
    image: parsed.image,
    layout: parsed.layout,
    theme: { mode: 'dark', accent: 'violet' },
  };
  const text = serializeProjectFile(state);
  const again = parseProjectFile(text);
  assert.equal(again.ok, true);
  assert.equal(again.project.name, 'red dot');
  assert.equal(again.project.color, '#7c3aed');
  assert.deepEqual(again.project.keywords, ['a', 'b']);
  assert.equal(again.project.source, 'https://ex/a.png');
  assert.equal(again.project.layout.lines.length, 1);
  assert.equal(again.project.layout.imageFilter, 'none');
  assert.deepEqual(again.project.theme, { mode: 'dark', accent: 'violet' });
});

test('theme is opt-in: omitted from the doc when state carries none', () => {
  const doc = buildProjectFile({ name: 'x', image: { dataUrl: RED_1x1, ext: 'png', w: 1, h: 1 }, layout: {} });
  assert.equal('theme' in doc, false);
  // And a doc WITH a theme keeps it.
  const themed = buildProjectFile({ name: 'x', image: { dataUrl: RED_1x1 }, layout: {}, theme: { mode: 'light', accent: 'sky' } });
  assert.deepEqual(themed.theme, { mode: 'light', accent: 'sky' });
});

test('theme: a custom hex accent is normalized; an invalid accent is dropped', () => {
  const a = buildProjectFile({ image: { dataUrl: RED_1x1 }, layout: {}, theme: { mode: 'dark', accent: '#ABCDEF' } });
  assert.deepEqual(a.theme, { mode: 'dark', accent: '#abcdef' });
  const b = buildProjectFile({ image: { dataUrl: RED_1x1 }, layout: {}, theme: { mode: 'dark', accent: 'not-a-color' } });
  assert.deepEqual(b.theme, { mode: 'dark' });
  const c = buildProjectFile({ image: { dataUrl: RED_1x1 }, layout: {}, theme: { accent: 'not-a-color' } });
  assert.equal('theme' in c, false);   // nothing valid left → no theme block
});

test('metadata is omitted when empty (minimal, diffable files)', () => {
  const doc = buildProjectFile({ name: 'x', image: { dataUrl: RED_1x1 }, layout: {} });
  assert.equal('color' in doc, false);
  assert.equal('keywords' in doc, false);
  assert.equal('source' in doc, false);
  assert.equal('blank' in doc, false);
});

test('blank projects round-trip their blank/blankColor hints', () => {
  const doc = buildProjectFile({ name: 'b', blank: true, blankColor: '#ffffff', image: { dataUrl: RED_1x1 }, layout: {} });
  assert.equal(doc.blank, true);
  assert.equal(doc.blankColor, '#ffffff');
  const res = parseProjectFile(doc);
  assert.equal(res.project.blank, true);
  assert.equal(res.project.blankColor, '#ffffff');
});

test('reject: missing format marker', () => {
  const doc = minimalDoc();
  delete doc.format;
  const res = parseProjectFile(doc);
  assert.equal(res.ok, false);
  assert.match(res.error, /Stencil project file/);
});

test('reject: a newer file version than we support', () => {
  const doc = minimalDoc();
  doc.version = STENCIL_FILE_VERSION + 1;
  const res = parseProjectFile(doc);
  assert.equal(res.ok, false);
  assert.match(res.error, /newer Stencil/);
});

test('reject: no embedded image', () => {
  const doc = minimalDoc();
  delete doc.image;
  assert.equal(parseProjectFile(doc).ok, false);
  // A non-data: URL is not a valid image either.
  const bad = minimalDoc();
  bad.image = { dataUrl: 'https://evil.example/x.png', ext: 'png' };
  assert.equal(parseProjectFile(bad).ok, false);
});

test('reject: not JSON', () => {
  const res = parseProjectFile('{ not json');
  assert.equal(res.ok, false);
  assert.match(res.error, /JSON/);
});

test('security: hostile lines are sanitized (prototype pollution stripped, coords coerced)', () => {
  const doc = minimalDoc();
  const evil = JSON.parse('{"points":[{"x":"5","y":"6"},{"x":"bad","y":1}],"__proto__":{"polluted":true},"color":"#0f0"}');
  doc.layout.lines = [evil];
  const res = parseProjectFile(doc);
  assert.equal(res.ok, true);
  const line = res.project.layout.lines[0];
  assert.equal(({}).polluted, undefined, 'Object.prototype must not be polluted');
  assert.equal(line.polluted, undefined);
  // "5"/"6" coerce to numbers; the NaN point is dropped.
  assert.deepEqual(line.points, [{ x: 5, y: 6 }]);
  assert.equal(line.color, '#0f0');
});
