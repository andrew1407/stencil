// The status line (js/console/shell/status.js): the Zig console's `image: …` line, with the
// history position, the temporary tag and the project's colour.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { statusLine, NO_IMAGE_STATUS, imageName } from '../../../js/console/shell/status.js';

const app = (over = {}) => ({
  canvas: { width: 1280, height: 720 }, image: { width: 1280, height: 720 }, imageBaseName: 'photo', imageExt: 'jpg', pageSize: 'A4',
  customPageWidth: 0, customPageHeight: 0, activeProjectId: 'p1',
  storage: { incognito: false, store: { getMeta: (id) => (id === 'p1' ? { name: 'Plans', color: '#ff0000', imageExt: 'jpg' } : null) } },
  history: { history: [{}, {}], historyStep: 1 },
  ...over,
});

test('no image says so', () => {
  assert.deepEqual(statusLine(app({ image: null })), { text: NO_IMAGE_STATUS, name: '', color: '', temporary: false });
});

test('the line names the picture, its size, its page, and where the edits stand', () => {
  const s = statusLine(app());
  assert.equal(s.name, 'Plans.jpg');
  assert.equal(s.color, '#ff0000');
  assert.equal(s.temporary, false);
  assert.match(s.text, /^image: Plans\.jpg \(1280x720 px · A4 29\.7×21cm\)  \[3\/3\]$/);
});

test('an unsaved picture is temporary, at the original there is no position, a nameless one is "image"', () => {
  const s = statusLine(app({ activeProjectId: null, history: { history: [], historyStep: -1 } }));
  assert.equal(s.temporary, true);
  assert.match(s.text, /\[in-memory, temporary\]$/);
  assert.doesNotMatch(s.text, /\[\d+\/\d+\]/);
  assert.equal(imageName(app({ activeProjectId: null, imageBaseName: '', imageExt: '' })), 'image');
});
