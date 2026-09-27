// The canvas coordinate tooltip's markup (js/ui/tip/tooltip.js): show() and showLine() format
// through the app's shared unit helpers, and the table is only rebuilt when it changes. The
// compare-mode gate that decides WHETHER they run is compareTooltip.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { cmToUnit, unitLabel } from '../../../js/utils.js';
import { StencilTooltip } from '../../../js/ui/tip/tooltip.js';

test('the tooltip formatting is the app\'s own — no second implementation', () => {
  // One show()/showLine() pair, using the shared unit helpers; the gate only decides
  // WHETHER to call them.
  const tip = Object.create(StencilTooltip.prototype);
  let markup = '';
  Object.assign(tip, { setMarkup: (html) => { markup = html; }, querySelectorAll: () => [], reveal() {}, app: {
    tooltipEnabled: true, tooltipShowPage: true, unit: 'in',
    // A 21 × 29.7 cm page over 2100 × 2970 px: the real page mapping reads 1 cm per 100 px.
    pageSize: 'custom', customPageWidth: 21, customPageHeight: 29.7, canvas: { width: 2100, height: 2970 },
  } });
  tip.show(0, 0, 254, 508);
  assert.match(markup, new RegExp(`Page \\(${unitLabel('in')}\\)`), 'the shared unit label');
  assert.match(markup, new RegExp(`<td>${cmToUnit(2.54, 'in').toFixed(2)}</td>\\s*<td>${cmToUnit(5.08, 'in').toFixed(2)}</td>`),
    'the shared cm→unit conversion');
  tip.showLine(0, 0, { points: [{ x: 254, y: 0 }] }, false);
  assert.match(markup, /254, 0 px<\/td>\s*<td>1\.00, 0\.00 in<\/td>/, 'showLine formats through the same helpers');
});

// BR-9: the tooltip re-renders on every move it is up for; the same table is not rebuilt.
test('setMarkup writes innerHTML only when the markup changes', () => {
  const tip = Object.create(StencilTooltip.prototype);
  let writes = 0;
  Object.defineProperty(tip, 'innerHTML', { set() { writes++; }, get: () => '' });
  tip.setMarkup('<table>a</table>');
  tip.setMarkup('<table>a</table>');
  assert.equal(writes, 1, 'a move over the same point rebuilds nothing');
  tip.setMarkup('<table>b</table>');
  assert.equal(writes, 2);
});

// BR-9: the Ctrl cursor readout changes on every move; its table is built once and only the numbers move.
test('a coordinate move under the same rows rewrites the cells, not the table', () => {
  const tip = Object.create(StencilTooltip.prototype);
  let writes = 0;
  let html = '';
  const cells = [];
  Object.defineProperty(tip, 'innerHTML', { set(v) { writes++; html = v; }, get: () => html });
  tip.querySelectorAll = () => {
    cells.length = 0;
    for (const m of html.matchAll(/<td>([^<]*)<\/td>/g)) cells.push({ textContent: m[1] });
    return cells;
  };
  tip.reveal = () => {};
  tip.app = { tooltipEnabled: true, tooltipShowScreen: true, tooltipShowPage: true, unit: 'cm',
    pageSize: 'custom', customPageWidth: 21, customPageHeight: 29.7, canvas: { width: 2100, height: 2970 } };
  tip.show(0, 0, 100, 200);
  assert.equal(writes, 1);
  assert.deepEqual(cells.map((c) => c.textContent), ['100', '200', '1.00', '2.00']);
  tip.show(0, 0, 350, 425);
  assert.equal(writes, 1, 'the table is not rebuilt while it tracks');
  assert.deepEqual(cells.map((c) => c.textContent), ['350', '425', '3.50', '4.25']);
  tip.app.tooltipShowPage = false;
  tip.show(0, 0, 350, 425);
  assert.equal(writes, 2, 'a different set of rows builds a new table');
  tip.showLine(0, 0, { points: [{ x: 1, y: 2 }] }, false);
  tip.show(0, 0, 10, 20);
  assert.equal(writes, 4, 'a line tip in between hands the box back to a fresh table');
  assert.deepEqual(cells.map((c) => c.textContent), ['10', '20']);
});
