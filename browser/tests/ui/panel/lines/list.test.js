// The Lines tab's rows (js/ui/panel/lines/list.js): each line is its number, its own line colour and
// thickness, its point colour and size, its point count and its bin, every cell named by a tooltip,
// with the selection marked through one Set.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installLinesTab, makeLinesApp } from '../../../helpers/linesTabRig.js';

const { body } = installLinesTab();
const { renderLinesList } = await import('../../../../js/ui/panel/lines/list.js');
const { default: constants } = await import('../../../../../common/config/constants.json', { with: { type: 'json' } });
const { thickMax, pointMax } = constants.LIMITS;

const rows = () => body.children;
const cellsOf = (i) => rows()[i].children;

test('a row is its number, line colour, thickness, point colour, point size, count and bin', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  const [index, lineCell, thick, pointCell, size, count, bin] = cellsOf(0);
  assert.equal(cellsOf(0).length, 7);
  assert.equal(index.textContent, '1');
  assert.equal(lineCell.children[0].className, 'lines-swatch');
  assert.deepEqual([lineCell.children[0].style.background, lineCell.children[0].style.borderColor], ['#ff0000', '#ff0000']);
  assert.deepEqual([thick.className, thick.dataset.prop, thick.textContent], ['lines-num', 'thickness', '3']);
  assert.equal(pointCell.children[0].className, 'lines-point-swatch');
  assert.equal(pointCell.children[0].style.background, '#ff0000', 'no point colour of its own: the line\'s');
  assert.deepEqual([size.dataset.prop, size.textContent], ['pointSize', '6']);
  assert.equal(count.textContent, '2');
  assert.equal(bin.children[0].getAttribute('aria-label'), 'Remove line 1');
  assert.equal(cellsOf(1)[3].children[0].style.background, '#00ff00', 'a point colour of its own');
  assert.equal(cellsOf(1)[1].children[0].style.background, '#FFFF00', 'a colourless line shows the default stroke');
});

test('every cell is named by a tooltip; the numbers say their range', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  const titles = cellsOf(0).slice(0, 6).map((c) => c.dataset.title.split('\n')[0]);
  assert.deepEqual(titles, ['Line 1', 'Line color', 'Line thickness', 'Point color', 'Point size', 'Points']);
  assert.match(cellsOf(0)[2].dataset.title, new RegExp(`–${thickMax} px`));
  assert.match(cellsOf(0)[4].dataset.title, new RegExp(`–${pointMax} px`));
  assert.equal(cellsOf(0)[6].children[0].dataset.title, 'Remove line');
});

test('an area keeps a compact marker: its fill inside the swatch, or a hollow ring without one', () => {
  const app = makeLinesApp();
  app.lines[0] = { ...app.lines[0], locked: true, fillColor: '#00ffff80' };
  app.lines[1] = { ...app.lines[1], locked: true, fillColor: 'transparent' };
  delete app.lines[2].pointSize;
  renderLinesList(app);
  const swatch = (i) => cellsOf(i)[1].children[0].style;
  assert.deepEqual([swatch(0).background, swatch(0).borderColor], ['#00ffff80', '#ff0000']);
  assert.equal(swatch(1).background, 'transparent');
  assert.match(cellsOf(0)[1].dataset.title, /^Line color — an area/);
  assert.equal(cellsOf(2)[4].textContent, '4', 'a line with no size of its own shows the toolbar\'s');
});

test('the selection is marked through one Set; an empty list is one message across every column', () => {
  const app = makeLinesApp();
  renderLinesList(app);
  assert.deepEqual(rows().map((r) => r.classList.contains('lines-row-selected')), [true, false, true]);
  app.lines = [];
  renderLinesList(app);
  assert.equal(rows().length, 1);
  assert.deepEqual([rows()[0].children[0].className, rows()[0].children[0].colSpan], ['empty-message', 7]);
});
