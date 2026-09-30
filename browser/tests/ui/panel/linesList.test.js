// The Lines tab (js/ui/panel/linesList.js): one delegated listener set on the list body however
// often it re-renders, each row answering hover, click, Delete and its bin through it with the
// real selection and removal functions, and the selection marked through one Set.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';

// closest() over the stub tree, for the selectors the list asks.
const MATCH = {
  '.lines-remove': (n) => n.classList?.contains('lines-remove'),
  '.lines-swatch': (n) => n.classList?.contains('lines-swatch'),
  'tr.lines-row': (n) => n.tagName === 'TR' && n.classList?.contains('lines-row'),
};
const withClosest = (el) => {
  el.closest = (sel) => {
    for (let n = el; n; n = n.parentNode) if (MATCH[sel](n)) return n;
    return null;
  };
  el.showPicker = () => { el.picks = (el.picks ?? 0) + 1; };
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
const { default: constants } = await import('../../../../common/config/constants.json', { with: { type: 'json' } });
const { doubleClickMs: DOUBLE_CLICK_MS, doubleTapMs: DOUBLE_TAP_MS } = constants.POPOVER;

const line = (color) => ({ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color });
const makeApp = () => {
  const app = {
    lines: [line('#ff0000'), line(''), line('#0000ff80')], color: '#00ff00',
    selectedLines: [0, 2], selectedLineIdx: -1, hoverLineIdx: -1, listHoverLineIdx: -1,
    coordLineIdx: -1, focusedPtIdx: -1, defaultFillColor: '#ffffff', saved: 0, shown: 0,
    renderer: { redraw() {} }, coordTable: { update() {} }, showSelectionPanel() { app.shown++; },
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

const swatchOf = (i) => at(i).children[1].children[0];
const picker = () => table.children.find((c) => c.className === 'lines-color-picker');

test('a swatch click selects its line and opens the picker once the double-click window passes', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = makeApp();
  renderLinesList(app);
  body.dispatch('click', { target: swatchOf(2) });
  assert.equal(app.selectedLineIdx, -1, 'nothing moves the list before a second click could land');
  t.mock.timers.tick(DOUBLE_CLICK_MS);
  assert.deepEqual([app.selectedLines, app.selectedLineIdx], [[], 2]);
  const input = picker();
  assert.equal(input.picks, 1);
  assert.equal(input.value, '#0000ff', 'the picker starts on the line colour, without its opacity');
  input.value = '#ff00ff';
  input.dispatch('input');
  assert.deepEqual([app.lines[2].color, app.saved], ['#ff00ff80', 0], 'a live pick previews, keeping the opacity');
  input.dispatch('change');
  assert.deepEqual([app.lines[2].color, app.saved, app.shown > 0], ['#ff00ff80', 1, true]);
});

test('a second swatch click inside the window resets the line to the toolbar colour', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = makeApp();
  renderLinesList(app);
  const before = picker()?.picks ?? 0;
  body.dispatch('click', { target: swatchOf(0) });
  body.dispatch('click', { target: swatchOf(0) });
  t.mock.timers.tick(DOUBLE_TAP_MS);
  assert.deepEqual([app.lines[0].color, app.saved], ['#00ff00', 1]);
  assert.equal(picker().picks, before, 'the double never opens the picker');
});

test('a tap waits the longer tap window; a read-only compare view never recolours', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = makeApp();
  renderLinesList(app);
  body.dispatch('click', { target: swatchOf(1), pointerType: 'touch' });
  t.mock.timers.tick(DOUBLE_CLICK_MS);
  body.dispatch('click', { target: swatchOf(1), pointerType: 'touch' });
  assert.equal(app.lines[1].color, '#00ff00');
  app.compareReadOnly = () => true;
  body.dispatch('click', { target: swatchOf(0) });
  body.dispatch('click', { target: swatchOf(0) });
  assert.deepEqual([app.lines[0].color, app.saved], ['#ff0000', 1]);
});
