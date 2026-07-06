// Unit tests for buildHandoff — the single editor/crop hand-off payload builder shared
// by the popup (sendToEditor / sendToEditorModal / openCrop) and the background service
// worker (PAGE_OPEN relay, video-preview click, context-menu click). Asserts the payload
// shape + the folded shared-vs-page provenance rule, and that `open` is omitted unless set.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildHandoff } from '../src/lib/stencil.js';

test('page image (non-shared): derives source via sourceOf, resource from the caller', () => {
  const image = { name: 'cat.png', kind: 'img', src: 'https://a.com/cat.png' };
  const payload = buildHandoff(image, { dataUrl: 'data:image/png;base64,AAA', page: 'A3', resource: 'https://a.com/page', incognito: false });
  assert.deepEqual(payload, {
    dataUrl: 'data:image/png;base64,AAA',
    name: 'cat.png',
    page: { size: 'A3' },
    source: 'https://a.com/cat.png',
    resource: 'https://a.com/page',
    incognito: false,
  });
});

test('video (non-shared): sourceOf uses the media URL, not the frame data URL', () => {
  const image = { name: 'clip.mp4', kind: 'video', videoUrl: 'https://a.com/clip.mp4', src: 'data:image/jpeg;base64,FRAME' };
  const payload = buildHandoff(image, { dataUrl: 'data:image/jpeg;base64,FRAME', page: 'A4', resource: 'https://a.com/watch', incognito: true });
  assert.equal(payload.source, 'https://a.com/clip.mp4');
  assert.equal(payload.resource, 'https://a.com/watch');
  assert.equal(payload.incognito, true);
});

test('shared (server) row: keeps its own source AND resource, ignores the caller resource', () => {
  const image = { name: 'proj.png', shared: true, source: 'https://srv/img/proj.png', resource: 'https://origin.example/page', src: 'ignored' };
  const payload = buildHandoff(image, { dataUrl: 'data:...', page: 'A3', resource: 'https://active.tab/now', incognito: false });
  assert.equal(payload.source, 'https://srv/img/proj.png');
  assert.equal(payload.resource, 'https://origin.example/page');
});

test('pre-resolved descriptor (background relay): explicit source is used verbatim', () => {
  // Background passes a plain { name, source } — no .shared, no .src — so buildHandoff
  // must take image.source directly rather than routing through sourceOf (which would '').
  const image = { name: 'image.png', source: 'https://a.com/bg.png' };
  const payload = buildHandoff(image, { dataUrl: 'data:x', page: 'A3', resource: 'https://a.com/p', incognito: false });
  assert.equal(payload.source, 'https://a.com/bg.png');
});

test('empty pre-resolved source stays empty (not re-derived to sourceOf)', () => {
  const payload = buildHandoff({ name: 'image.png', source: '' }, { dataUrl: 'd', page: 'A3', resource: 'r', incognito: false });
  assert.equal(payload.source, '');
});

test('open is omitted when undefined, present when set', () => {
  const image = { name: 'x', src: 'https://a/x.png' };
  const plain = buildHandoff(image, { dataUrl: 'd', page: 'A3', resource: 'r' });
  assert.ok(!('open' in plain), 'open key absent for a plain open');

  const resume = buildHandoff(image, { dataUrl: 'd', page: 'A3', resource: 'r', open: 'resume' });
  assert.equal(resume.open, 'resume');

  const copy = buildHandoff(image, { dataUrl: 'd', page: 'A3', resource: 'r', open: 'copy' });
  assert.equal(copy.open, 'copy');
});

test('incognito is coerced to a real boolean', () => {
  const image = { name: 'x', src: 'https://a/x.png' };
  assert.equal(buildHandoff(image, {}).incognito, false);
  assert.equal(buildHandoff(image, { incognito: true }).incognito, true);
});

test('page size is wrapped as { size }', () => {
  const payload = buildHandoff({ name: 'x', src: 'https://a/x.png' }, { page: 'A5' });
  assert.deepEqual(payload.page, { size: 'A5' });
});
