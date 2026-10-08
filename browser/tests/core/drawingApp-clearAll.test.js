// Clear All Lines (js/core/drawingApp.js clearAllLines): the toolbar click and the hotkey ask first,
// and a refusal keeps every line; `{ ask: false }` (the eraser dropped on the canvas) clears at once.
// Either way it is one history step and the same notice.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../helpers/dom.js';

const doc = installDom({ autoCreateById: true }, {
  location: { hash: '', pathname: '/', search: '' }, history: { replaceState: () => {} },
  matchMedia: () => ({ matches: true }),
});
doc.querySelectorAll = () => [];
const notices = [];
doc.register('notify-balloon', createStubElement('div', { notify: (msg, type) => notices.push([msg, type]) }));

const { DrawingApp } = await import('../../js/core/drawingApp.js');
const { Emitter } = await import('../../js/core/emitter.js');

const makeApp = (answer = true) => {
  const calls = [];
  const app = Object.create(DrawingApp.prototype);
  Object.assign(app, {
    lines: [{ points: [{ x: 1, y: 1 }, { x: 9, y: 9 }] }, { points: [{ x: 4, y: 0 }, { x: 4, y: 8 }] }],
    currentLine: null, selectedLineIdx: 0, selectedLines: [],
    changes: new Emitter(), strokeFx: { cancel() {} }, coordTable: { update() {} },
    renderer: { redraw() {}, effectiveCompareMode: () => 'none' },
    hideSelectionPanels() {},
    saveHistory: () => calls.push('history'),
    confirm: async () => { calls.push('confirm'); return answer; },
  });
  return { app, calls };
};

test('a click asks first, then clears every line in one step', async () => {
  notices.length = 0;
  const { app, calls } = makeApp(true);
  await app.clearAllLines();
  assert.deepEqual(calls, ['confirm', 'history']);
  assert.equal(app.lines.length, 0);
  assert.equal(app.selectedLineIdx, -1);
  assert.deepEqual(notices.at(-1), ['All lines cleared', 'ok']);
});

test('a refused confirmation keeps every line', async () => {
  const { app, calls } = makeApp(false);
  await app.clearAllLines();
  assert.deepEqual(calls, ['confirm']);
  assert.equal(app.lines.length, 2);
});

test('ask: false clears at once, with the same step and notice', async () => {
  notices.length = 0;
  const { app, calls } = makeApp(false);
  await app.clearAllLines({ ask: false });
  assert.deepEqual(calls, ['history'], 'no confirmation');
  assert.equal(app.lines.length, 0);
  assert.deepEqual(notices, [['All lines cleared', 'ok']]);
});
