// The Points and Lines rows' hover and cursors (css/layout/coord/rows.css, panel.css, webcore/menus.css),
// against the desktop's SelectionPanel: a hovered row takes the accent at 45/255 (rowWash), its lone
// message row the neutral grey, the skin's light face under webcore; a cell a double-click edits
// shows the I-beam (SelectionPanel::cellCursor).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { LAYOUT_CSS } from '../../helpers/css.js';

const WEBCORE = readFileSync(new URL('../../../css/webcore/menus.css', import.meta.url), 'utf8');
const ruleOf = (css, selector) => {
  const at = css.indexOf(`${selector} {`);
  assert.ok(at >= 0, `${selector} is styled`);
  return css.slice(at, css.indexOf('}', at));
};
const ACCENT_WASH = 'color-mix(in srgb, var(--accent) 18%, transparent)';

test('a hovered Points or Lines row takes the accent wash the desktop paints, in every theme', () => {
  for (const sel of ['.coordinates-table tbody tr:hover', '.lines-row:hover'])
    assert.ok(ruleOf(LAYOUT_CSS, sel).includes(`background-color: ${ACCENT_WASH}`), sel);
  assert.ok(ruleOf(LAYOUT_CSS, '.coordinates-table tbody tr:has(.empty-message):hover')
    .includes('var(--bg-coord-hover)'), 'the lone message row hovers grey');
});

test('under the webcore skin a hovered row is the light face, a selected one keeps the navy bar', () => {
  const rule = WEBCORE.slice(WEBCORE.indexOf(':root[data-skin="webcore"] :is(.coordinates-table tbody tr.row-highlighted'));
  const head = rule.slice(0, rule.indexOf('{'));
  assert.ok(head.includes('.coordinates-table tbody tr:hover:not(.row-focused):not(.lines-row-selected)'));
  assert.ok(head.includes('.lines-row:hover:not(.lines-row-selected)'));
  assert.match(rule.slice(0, rule.indexOf('}')), /background: var\(--wc-light\) !important/);
});

test('the cells a double-click edits show the I-beam; the rest of a row keeps the hand', () => {
  assert.ok(ruleOf(LAYOUT_CSS, '.coordinates-table td:is(.cell-px-x, .cell-px-y)').includes('cursor: text'));
  assert.ok(ruleOf(LAYOUT_CSS, '.lines-num').includes('cursor: text'));
  assert.ok(ruleOf(LAYOUT_CSS, '.lines-row').includes('cursor: pointer'));
  assert.ok(ruleOf(LAYOUT_CSS, '.coordinates-table tbody tr:hover').includes('cursor: pointer'));
});
