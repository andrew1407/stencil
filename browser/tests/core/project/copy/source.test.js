// liveProjectSnapshot (core/project/copy/source.js): the live editor as a copy source — its saved
// row's meta under what is on screen, and a chat owner only when that row is really saved.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../../helpers/dom.js';
import { liveProjectSnapshot } from '../../../../js/core/project/copy/source.js';

installDom();

const IMG = 'data:image/png;base64,AA';
const app = ({ incognito = false, temporary = false, active = 'p1', meta = null } = {}) => ({
  activeProjectId: active, imageDataUrl: IMG, imageBaseName: 'shot', blankColor: '', fromFile: false,
  canvas: { width: 40, height: 30 }, lines: [{ points: [{ x: 1, y: 2 }] }], rotationQuarters: 0,
  storage: { incognito, temporary, store: { getMeta: (id) => (id === 'p1' ? meta : null) } },
});

test('a saved project: its row names the copy and owns the chat, the lines are what is on screen', () => {
  const snap = liveProjectSnapshot(app({ meta: { name: 'Saved', color: '#112233', thumbnail: 'data:image/jpeg;base64,/9j' } }));
  assert.equal(snap.srcId, 'p1');
  assert.deepEqual([snap.meta.name, snap.meta.color, snap.meta.thumbnail], ['Saved', '#112233', 'data:image/jpeg;base64,/9j']);
  assert.equal(snap.payload.image, IMG);
  assert.equal(snap.payload.layout.lines.length, 1);
});

for (const [name, over] of [['incognito', { incognito: true }], ['temporary', { temporary: true }], ['unsaved', { active: null }]]) {
  test(`an ${name} editor has no chat owner and ignores any stored row`, () => {
    const snap = liveProjectSnapshot(app({ ...over, meta: { name: 'Saved', color: '#112233' } }));
    assert.equal(snap.srcId, null);
    assert.deepEqual([snap.meta.name, snap.meta.color], ['shot', '']);
  });
}
