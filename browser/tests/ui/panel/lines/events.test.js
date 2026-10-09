// The Lines tab's gestures (js/ui/panel/lines/events.js): one delegated listener set on the list body
// however often it re-renders, each row answering hover, click, Delete, its bin, its two colour
// swatches and its numbers through the real selection, removal and line-edit functions.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installLinesTab, makeLinesApp } from '../../../helpers/linesTabRig.js';

const { body, table } = installLinesTab();
const { renderLinesList } = await import('../../../../js/ui/panel/lines/list.js');
const { default: constants } = await import('../../../../../common/config/constants.json', { with: { type: 'json' } });
const { doubleClickMs: DOUBLE_CLICK_MS, doubleTapMs: DOUBLE_TAP_MS } = constants.POPOVER;

const at = (i) => body.children[i];
const cellOf = (row, col) => at(row).children[col];
const lineSwatch = (i) => cellOf(i, 2).children[0];
const pointSwatch = (i) => cellOf(i, 4).children[0];
const picker = () => table.children.find((c) => c.className === 'lines-color-picker');

test('re-rendering keeps one listener per event type on the body, none on a row', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  renderLinesList(app);
  for (const t of ['mouseover', 'mouseout', 'click', 'dblclick', 'keydown']) assert.equal(body.listeners[t].length, 1, t);
  assert.ok(body.children.every((r) => Object.keys(r.listeners).length === 0));
});

test('hover, click and Delete reach the row under the event; a number cell never selects', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  const cell = cellOf(2, 0);
  body.dispatch('mouseover', { target: cell, relatedTarget: at(1) });
  assert.equal(app.listHoverLineIdx, 2);
  body.dispatch('mouseout', { target: cell, relatedTarget: at(2).children[6] });
  assert.equal(app.listHoverLineIdx, 2, 'a move between cells of one row is no leave');
  body.dispatch('mouseout', { target: cell, relatedTarget: null });
  assert.equal(app.listHoverLineIdx, -1);
  body.dispatch('click', { target: cellOf(1, 1) });
  body.dispatch('click', { target: cellOf(1, 3) });
  body.dispatch('click', { target: cellOf(1, 5) });
  assert.deepEqual([app.selectedLines, app.selectedLineIdx], [[0, 2], -1], 'the name and numbers edit, they do not select');
  body.dispatch('click', { target: cell, ctrlKey: false, metaKey: false, shiftKey: false });
  assert.deepEqual([app.selectedLines, app.selectedLineIdx], [[], 2]);
  let stopped = 0;
  body.dispatch('keydown', { target: at(1), key: 'Delete', preventDefault() {}, stopPropagation() { stopped++; } });
  assert.deepEqual([app.lines.length, stopped, app.saved], [2, 1, 1]);
});

test('the bin removes its own row and never selects it', async () => {
  const app = makeLinesApp();
  renderLinesList(app);
  const bin = cellOf(0, 8).children[0];
  let stopped = 0;
  body.dispatch('click', { target: bin, stopPropagation() { stopped++; } });
  await new Promise((r) => setTimeout(r, 400));
  // The selection [0, 2] keeps line 3 alone, now the second row: a single selection again.
  assert.deepEqual([app.lines.length, app.lines[0].color, stopped, app.selectedLineIdx], [2, '', 1, 1]);
});

test('a line swatch click selects its line and opens the picker once the double-click window passes', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = makeLinesApp();
  renderLinesList(app);
  body.dispatch('click', { target: lineSwatch(2) });
  assert.equal(app.selectedLineIdx, -1, 'nothing moves the list before a second click could land');
  t.mock.timers.tick(DOUBLE_CLICK_MS);
  assert.deepEqual([app.selectedLines, app.selectedLineIdx], [[], 2]);
  const input = picker();
  assert.equal(input.picks, 1);
  assert.equal(input.value, '#0000ff', 'the picker starts on the line colour, without its opacity');
  input.value = '#ff00ff';
  input.dispatch('input');
  assert.deepEqual([app.lines[2].color, app.saved], ['#ff00ff80', 0], 'a live pick previews, keeping the opacity');
  const shown = app.shown;
  input.dispatch('change');
  assert.deepEqual([app.lines[2].color, app.saved, app.shown], ['#ff00ff80', 1, shown + 1], 'the bar follows the commit');
});

test('a second line-swatch click inside the window resets the line to the toolbar colour', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = makeLinesApp();
  renderLinesList(app);
  const before = picker()?.picks ?? 0;
  body.dispatch('click', { target: lineSwatch(0) });
  body.dispatch('click', { target: lineSwatch(0) });
  t.mock.timers.tick(DOUBLE_TAP_MS);
  assert.deepEqual([app.lines[0].color, app.saved], ['#00ff00', 1]);
  assert.equal(picker().picks, before, 'the double never opens the picker');
});

test('the point swatch picks that line\'s point colour and keeps the selection; a double gives the points the line\'s', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = makeLinesApp();
  renderLinesList(app);
  const before = picker()?.picks ?? 0;
  body.dispatch('click', { target: pointSwatch(1) });
  t.mock.timers.tick(DOUBLE_CLICK_MS);
  const input = picker();
  assert.deepEqual([input.picks, input.value], [before + 1, '#00ff00'], 'the picker starts on the point colour');
  input.value = '#123456';
  input.dispatch('change');
  assert.deepEqual([app.lines[1].pointColor, app.lines[1].color, app.saved], ['#123456', '', 1]);
  assert.deepEqual([app.selectedLines, app.selectedLineIdx, app.shown], [[0, 2], -1, 0], 'the selection is untouched');
  body.dispatch('click', { target: pointSwatch(1) });
  body.dispatch('click', { target: pointSwatch(1) });
  t.mock.timers.tick(DOUBLE_TAP_MS);
  assert.deepEqual([app.lines[1].pointColor, app.saved, input.picks], ['', 2, before + 1]);
});

test('a tap waits the longer tap window; a read-only compare view never recolours', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const app = makeLinesApp();
  renderLinesList(app);
  body.dispatch('click', { target: lineSwatch(1), pointerType: 'touch' });
  t.mock.timers.tick(DOUBLE_CLICK_MS);
  body.dispatch('click', { target: lineSwatch(1), pointerType: 'touch' });
  assert.equal(app.lines[1].color, '#00ff00');
  app.readOnly = true;
  for (const swatch of [lineSwatch(0), lineSwatch(0), pointSwatch(0), pointSwatch(0)]) body.dispatch('click', { target: swatch });
  assert.deepEqual([app.lines[0].color, app.lines[0].pointColor, app.saved], ['#ff0000', undefined, 1]);
});

test('a swatch dragged away inside the window opens no picker and selects nothing', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { createIconDrag } = await import('../../../../js/ui/drag/iconDrag.js');
  const dragAway = () => {
    const m = createIconDrag({ originRect: () => ({ left: 0, top: 0, right: 10, bottom: 10 }) });
    m.press(5, 5);
    m.move(200, 200);
    m.release(200, 200);
  };
  const app = makeLinesApp();
  renderLinesList(app);
  const before = picker()?.picks ?? 0;
  for (const swatch of [lineSwatch, pointSwatch]) {
    body.dispatch('click', { target: swatch(2) });
    dragAway();
    t.mock.timers.tick(DOUBLE_CLICK_MS);
  }
  assert.deepEqual([picker()?.picks ?? 0, app.selectedLines, app.selectedLineIdx], [before, [0, 2], -1]);
  body.dispatch('click', { target: pointSwatch(2) });
  t.mock.timers.tick(DOUBLE_CLICK_MS);
  assert.equal(picker().picks, before + 1, 'a later click still opens it');
});

test('a double-click on a number opens its field in place', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  const thick = cellOf(0, 3);
  body.dispatch('dblclick', { target: thick });
  const input = thick.children[0];
  assert.deepEqual([input.tagName, input.className, input.value], ['INPUT', 'lines-num-input', '3']);
  body.dispatch('dblclick', { target: cellOf(0, 0) });
  assert.equal(cellOf(0, 0).children.length, 0, 'the number of the row is no field');
});

test('the eye hides and shows its line as one step each, never selects, and plays on its own row once', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  let stopped = 0;
  body.dispatch('click', { target: cellOf(1, 7).children[0], stopPropagation() { stopped++; } });
  assert.deepEqual([app.lines[1].hidden, app.saved, stopped, app.selectedLineIdx], [true, 1, 1, -1]);
  renderLinesList(app);
  const eye = cellOf(1, 7).children[0];
  assert.ok(at(1).classList.contains('lines-row-hidden'));
  assert.ok(eye.classList.contains('lines-eye-off') && eye.classList.contains('lines-eye-flip'));
  assert.deepEqual([eye.dataset.title, eye.getAttribute('aria-pressed')], ['Show line', 'true']);
  renderLinesList(app);
  assert.ok(!cellOf(1, 7).children[0].classList.contains('lines-eye-flip'), 'a later render rests');
  body.dispatch('click', { target: cellOf(1, 7).children[0], stopPropagation() {} });
  assert.deepEqual([app.lines[1].hidden, app.saved], [false, 2]);
  app.readOnly = true;
  body.dispatch('click', { target: cellOf(1, 7).children[0], stopPropagation() {} });
  assert.deepEqual([app.lines[1].hidden, app.saved], [false, 2], 'a read-only compare view hides nothing');
});

test('a double-click on the name opens its field on the name shown; Enter renames as one step, Escape keeps it', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  const name = cellOf(0, 1);
  assert.deepEqual([name.textContent, name.classList.contains('lines-name-unset')], ['Line 1', true]);
  body.dispatch('dblclick', { target: name });
  const input = name.children[0];
  assert.deepEqual([input.className, input.value, input.placeholder], ['lines-name-input', 'Line 1', 'Line 1'],
    "an unnamed line's field opens on its own \"Line 1\"");
  input.dispatch('keydown', { key: 'Enter', preventDefault() {}, stopPropagation() {} });
  assert.deepEqual([app.lines[0].name ?? '', app.saved], ['', 0], 'committed untouched, it stays unnamed');
  renderLinesList(app);
  body.dispatch('dblclick', { target: cellOf(0, 1) });
  const typing = cellOf(0, 1).children[0];
  typing.value = '  Roof ridge  ';
  typing.dispatch('keydown', { key: 'Enter', preventDefault() {}, stopPropagation() {} });
  input.dispatch('keydown', { key: 'Enter', preventDefault() {}, stopPropagation() {} });
  assert.deepEqual([app.lines[0].name, app.saved], ['Roof ridge', 1]);
  renderLinesList(app);
  const again = cellOf(0, 1);
  assert.deepEqual([again.textContent, again.classList.contains('lines-name-unset')], ['Roof ridge', false]);
  body.dispatch('dblclick', { target: again });
  again.children[0].value = 'Other';
  again.children[0].dispatch('keydown', { key: 'Escape', preventDefault() {}, stopPropagation() {} });
  assert.deepEqual([app.lines[0].name, app.saved, again.textContent], ['Roof ridge', 1, 'Roof ridge']);
});
