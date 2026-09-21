// buildProjectMeta (js/core/projectMeta.js) rebuilds the WHOLE registry row on every save,
// and ProjectsStore.upsert replaces the row wholesale — so any field the row owns but the
// live app does not must be carried over from `prev`, or a plain edit silently erases it.
// Keywords were the field that got missed (an edit reset them, breaking keyword search).
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { buildProjectMeta } from '../js/core/project/meta/projectMeta.js';

// Only what buildProjectMeta reads. `thumbnail` is passed explicitly so makeThumbnail —
// the one DOM/canvas caller — never runs.
const fakeApp = (over = {}) => ({
  imageBaseName: 'photo',
  imageDataUrl: 'data:image/png;base64,AA',
  imageSource: null,
  imageResource: null,
  blankColor: '',
  fromFile: false,
  remoteLink: null,
  canvas: { width: 800, height: 600 },
  ...over,
});

const build = (prev, app = fakeApp()) =>
  buildProjectMeta(app, { prev, id: 'p1', layout: {}, thumbnail: 'thumb' });

// Every field the row owns that a plain save must carry over untouched.
const PRESERVED = {
  name: 'Kept name',
  color: '#22c55e',
  description: 'kept description',
  keywords: ['alpha', 'Beta'],
  createdAt: 1700000000000,
  expiresAt: 1800000000000,
  refreshPeriod: 'month',
  autoRefresh: false,
};

test('a plain save preserves every user-owned field of the row', () => {
  const meta = build({ id: 'p1', ...PRESERVED });
  for (const [key, value] of Object.entries(PRESERVED)) {
    assert.deepEqual(meta[key], value, `${key} survives a save`);
  }
});

test('keywords survive a save, and are not shared with the previous row', () => {
  const prev = { id: 'p1', keywords: ['alpha', 'beta'] };
  const meta = build(prev);
  assert.deepEqual(meta.keywords, ['alpha', 'beta']);
  // Mutating the new row must not reach back into the registry row it came from.
  meta.keywords.push('gamma');
  assert.deepEqual(prev.keywords, ['alpha', 'beta']);
});

test('an absent or malformed keyword list normalises to []', () => {
  assert.deepEqual(build({}).keywords, []);
  assert.deepEqual(build({ keywords: [] }).keywords, []);
  assert.deepEqual(build({ keywords: undefined }).keywords, []);
  // Same guard as ProjectsStore's own legacy-row normalisation.
  assert.deepEqual(build({ keywords: 'alpha,beta' }).keywords, []);
});

test('a first save (no prev row) starts from the app, not from nothing', () => {
  const meta = build({});
  assert.equal(meta.id, 'p1');
  assert.equal(meta.name, 'photo', 'falls back to the image base name');
  assert.equal(meta.color, '');
  assert.equal(meta.description, '');
  assert.deepEqual(meta.keywords, []);
  assert.equal(meta.hasImage, true);
  assert.equal(meta.imageW, 800);
  assert.equal(meta.imageH, 600);
  assert.equal(meta.thumbnail, 'thumb');
});

test('app-owned fields are re-read from the app, never carried over', () => {
  const app = fakeApp({ blankColor: '#ffffff', fromFile: true, imageDataUrl: null, canvas: { width: 10, height: 20 } });
  const meta = buildProjectMeta(app, { prev: { ...PRESERVED, blank: false, blankColor: '', fromFile: false, hasImage: true, imageW: 999, imageH: 999 }, id: 'p1', layout: {}, thumbnail: null });
  assert.equal(meta.blank, true);
  assert.equal(meta.blankColor, '#ffffff');
  assert.equal(meta.fromFile, true);
  assert.equal(meta.hasImage, false);
  assert.equal(meta.imageW, 10);
  assert.equal(meta.imageH, 20);
  // …while the user-owned ones still come from prev.
  assert.deepEqual(meta.keywords, PRESERVED.keywords);
  assert.equal(meta.description, PRESERVED.description);
});

test('the server link is re-read from the live remoteLink', () => {
  const app = fakeApp({ remoteLink: { address: 'https://s.example', remoteId: 'r1', version: 7 } });
  const meta = buildProjectMeta(app, { prev: {}, id: 'p1', layout: {}, thumbnail: null });
  assert.equal(meta.address, 'https://s.example');
  assert.equal(meta.remoteId, 'r1');
  assert.equal(meta.remoteVersion, 7);
});
