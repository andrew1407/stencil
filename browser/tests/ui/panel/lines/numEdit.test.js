// A Lines-row number edited in place (js/ui/panel/lines/numEdit.js): Enter or blur commits one
// history step through applyLineChange, clamped to LIMITS, and the bar follows its own line; Escape
// and an unchanged value commit nothing; the field keeps its keys from the row and the canvas.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installLinesTab, makeLinesApp } from '../../../helpers/linesTabRig.js';

installLinesTab();
const { editLineNumber } = await import('../../../../js/ui/panel/lines/numEdit.js');
const { default: constants } = await import('../../../../../common/config/constants.json', { with: { type: 'json' } });
const { thickMax, pointMin } = constants.LIMITS;

const numCell = (prop, text) => {
  const td = document.createElement('td');
  td.className = 'lines-num';
  td.dataset.prop = prop;
  td.textContent = text;
  return td;
};
const open = (app, prop, idx = 0) => {
  const cell = numCell(prop, String(app.lines[idx][prop]));
  editLineNumber(app, cell, idx);
  return { cell, input: cell.children[0] };
};
const key = (input, k) => {
  const ev = { key: k, stopped: 0, preventDefault() {}, stopPropagation() { ev.stopped++; } };
  input.dispatch('keydown', ev);
  return ev;
};

test('Enter commits one step, clamped to LIMITS; the bar follows the line it shows', () => {
  const app = makeLinesApp();
  app.selectedLines = [];
  app.selectedLineIdx = 0;
  const { cell, input } = open(app, 'thickness');
  assert.deepEqual([input.min, input.max, input.step, input.focused], ['1', String(thickMax), '1', true]);
  input.value = String(thickMax + 40);
  const ev = key(input, 'Enter');
  assert.deepEqual([app.lines[0].thickness, app.saved, app.shown], [thickMax, 1, 1]);
  assert.equal(cell.textContent, String(thickMax));
  assert.equal(ev.stopped, 1, 'the key stays in the field');
  input.dispatch('blur');
  assert.equal(app.saved, 1, 'the blur after the commit adds nothing');
});

test('blur commits on another line and leaves the selection and the bar alone', () => {
  const app = makeLinesApp();
  const { input } = open(app, 'pointSize', 1);
  input.value = '0';
  input.dispatch('blur');
  assert.deepEqual([app.lines[1].pointSize, app.saved, app.shown, app.selectedLines], [pointMin, 1, 0, [0, 2]]);
});

test('Escape, an unchanged value or no number commit nothing; Backspace stays in the field', () => {
  const app = makeLinesApp();
  let { cell, input } = open(app, 'thickness');
  input.value = '9';
  assert.equal(key(input, 'Backspace').stopped, 1);
  key(input, 'Escape');
  assert.deepEqual([app.lines[0].thickness, app.saved, cell.textContent], [3, 0, '3']);
  ({ input } = open(app, 'thickness'));
  key(input, 'Enter');
  ({ input } = open(app, 'pointSize'));
  input.value = '';
  input.dispatch('blur');
  assert.deepEqual([app.lines[0].thickness, app.lines[0].pointSize, app.saved], [3, 6, 0]);
});

test('a read-only compare view, a cell already editing or one that names no size opens nothing', () => {
  const app = makeLinesApp();
  app.readOnly = true;
  assert.equal(open(app, 'thickness').cell.children.length, 0);
  app.readOnly = false;
  const { cell } = open(app, 'thickness');
  editLineNumber(app, cell, 0);
  assert.equal(cell.children.length, 1, 'one field at a time');
  assert.equal(open(app, 'color').cell.children.length, 0);
});

test('an untouched field keeps a fractional size; a typed one is whole, as the bar reads it', () => {
  const app = makeLinesApp();
  app.lines[0].thickness = 2.5;
  let { input } = open(app, 'thickness');
  key(input, 'Enter');
  assert.deepEqual([app.lines[0].thickness, app.saved], [2.5, 0]);
  ({ input } = open(app, 'thickness'));
  input.value = '4.8';
  key(input, 'Enter');
  assert.deepEqual([app.lines[0].thickness, app.saved], [4, 1]);
});

test('a line with no size of its own starts from the toolbar\'s', () => {
  const app = makeLinesApp();
  delete app.lines[2].pointSize;
  const cell = numCell('pointSize', '4');
  editLineNumber(app, cell, 2);
  assert.equal(cell.children[0].value, String(app.pointSize));
});
