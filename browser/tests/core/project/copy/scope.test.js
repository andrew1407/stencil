// copyPayload (core/project/copy/scope.js): what each scope carries into the new row.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { copyPayload } from '../../../../js/core/project/copy/scope.js';
import { IMG, LAYOUT, META } from '../../../helpers/projectCopyRig.js';

const src = () => ({ srcId: 'p1', meta: { ...META, blank: true, blankColor: '#ffffff', address: 'https://srv', remoteId: 'r1' },
  payload: { image: IMG, layout: JSON.parse(JSON.stringify(LAYOUT)) }, remote: null });
const at = { id: 'n1', name: 'Cat-copy', now: 1000 };

test('image: the original alone — no lines, crop, turn, filter or formulas; provenance and blank kept', () => {
  const { meta, payload } = copyPayload(src(), 'image', at);
  assert.equal(payload.image, IMG);
  assert.deepEqual(payload.layout, { lines: [], imageBaseName: 'cat', imageExt: 'png',
    imageSource: LAYOUT.imageSource, imageResource: LAYOUT.imageResource });
  assert.equal(meta.blank, true);
  assert.equal(meta.blankColor, '#ffffff');
  assert.equal(meta.source, META.source);
  assert.equal(meta.lineLengthCm, 0);
  assert.deepEqual([meta.color, meta.description, meta.keywords], ['', '', []]);
});

test('layout: the whole layout, deep-copied; the project meta stays fresh', () => {
  const s = src();
  const { meta, payload } = copyPayload(s, 'layout', at);
  assert.deepEqual(payload.layout, LAYOUT);
  assert.notEqual(payload.layout.lines, s.payload.layout.lines, 'the lines are a copy, not the source array');
  assert.equal(meta.color, '');
  assert.ok(meta.expiresAt > 1000, 'a fresh default expiry');
  assert.equal(meta.imageW, 40);
});

test('project: the layout plus colour, words and expiry settings', () => {
  const { meta } = copyPayload(src(), 'project', at);
  assert.equal(meta.color, '#112233');
  assert.equal(meta.description, 'a cat');
  assert.deepEqual(meta.keywords, ['pet']);
  assert.deepEqual([meta.expiresAt, meta.refreshPeriod, meta.autoRefresh], [0, 'month', false]);
});

test('every scope is a detached, fresh row under the given id and name', () => {
  for (const what of ['image', 'layout', 'project']) {
    const { meta } = copyPayload(src(), what, at);
    assert.deepEqual([meta.id, meta.name, meta.createdAt], ['n1', 'Cat-copy', 1000], what);
    assert.deepEqual([meta.address, meta.remoteId, meta.remoteVersion, meta.fromFile], [null, null, 0, false], what);
  }
});
