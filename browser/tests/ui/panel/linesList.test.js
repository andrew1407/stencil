// The Lines tab (js/ui/panel/linesList.js): one delegated listener set on the list body however
// often it re-renders, each row answering hover, click, Delete and its bin through it with the
// real selection and removal functions, and the selection marked through one Set.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';

// closest() over the stub tree, for the two selectors the list asks.
const withClosest = (el) => {
  el.closest = (sel) => {
    const want = sel === '.lines-remove' ? (n) => n.classList?.contains('lines-remove')
      : (n) => n.tagName === 'TR' && n.classList?.contains('lines-row');
    for (let n = el; n; n = n.parentNode) if (want(n)) return n;
    return null;
  };
  return el;
};

const body = createStubElement('tbody', {
  replaceChildren() { body.children.length = 0; },
  querySelectorAll: () => body.children.filter((r) => r.classList.contains('lines-row')),
});
const table = createStubElement('table', { tBodies: [body] });
globalThis.document = {
  getElementById: (id) => (id === 'lines-list' ? table : null),
  createElement: (tag) => withClosest(createStubElement(tag)),
};
const { renderLinesList } = await import('../../../js/ui/panel/linesList.js');

const line = (color) => ({ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color });
const makeApp = () => {
  const app = {
    lines: [line('#ff0000'), line(''), line('#0000ff')],
    selectedLines: [0, 2], selectedLineIdx: -1, hoverLineIdx: -1, listHoverLineIdx: -1,
    coordLineIdx: -1, focusedPtIdx: -1, defaultFillColor: '#ffffff', saved: 0,
    renderer: { redraw() {} }, coordTable: { update() {} }, showSelectionPanel() {},
    deselectLine() { app.selectedLineIdx = -1; }, saveHistory() { app.saved++; },
    compareReadOnly: () => false,
  };
  return app;
};
const rows = () => body.children;
const at = (i) => rows()[i];

test('re-rendering keeps one listener per event type on the body, none on a row', () => {
  const app = makeApp();
  renderLinesList(app);
  renderLinesList(app);
  for (const t of ['mouseover', 'mouseout', 'click', 'keydown']) assert.equal(body.listeners[t].length, 1, t);
  assert.ok(rows().every((r) => Object.keys(r.listeners).length === 0));
  assert.deepEqual(rows().map((r) => r.classList.contains('lines-row-selected')), [true, false, true]);
  assert.equal(at(1).children[1].children[0].style.background, '#FFFF00', 'a colourless line shows the default stroke');
});

test('hover, click and Delete reach the row under the event', () => {
  const app = makeApp();
  renderLinesList(app);
  const cell = at(2).children[2];
  body.dispatch('mouseover', { target: cell, relatedTarget: at(1) });
  assert.equal(app.listHoverLineIdx, 2);
  body.dispatch('mouseout', { target: cell, relatedTarget: at(2).children[0] });
  assert.equal(app.listHoverLineIdx, 2, 'a move between cells of one row is no leave');
  body.dispatch('mouseout', { target: cell, relatedTarget: null });
  assert.equal(app.listHoverLineIdx, -1);
  body.dispatch('click', { target: cell, ctrlKey: false, metaKey: false, shiftKey: false });
  assert.deepEqual([app.selectedLines, app.selectedLineIdx], [[], 2]);
  let stopped = 0;
  body.dispatch('keydown', { target: at(1), key: 'Delete', preventDefault() {}, stopPropagation() { stopped++; } });
  assert.deepEqual([app.lines.length, stopped, app.saved], [2, 1, 1]);
});

test('the bin removes its own row and never selects it', async () => {
  const app = makeApp();
  renderLinesList(app);
  const bin = at(0).children[4].children[0];
  let stopped = 0;
  body.dispatch('click', { target: bin, stopPropagation() { stopped++; } });
  await new Promise((r) => setTimeout(r, 400));
  assert.deepEqual([app.lines.length, app.lines[0].color, stopped, app.selectedLineIdx], [2, '', 1, -1]);
});
