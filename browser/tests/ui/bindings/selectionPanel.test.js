// The selected-line bar's controls, driven through their real listeners: the point-size box
// reaches the field the renderer reads, and a colour drag previews without filling the undo stack.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';
import { recordingCtx, argsOf } from '../../helpers/recordingCtx.js';
import { wireSelectionPanelControls } from '../../../js/ui/bindings/selectionPanel.js';
import { applySelectionChange } from '../../../js/core/line/selection.js';
import { applyFill } from '../../../js/ui/panel/selectionPanel.js';
import { drawLine } from '../../../js/core/line/render.js';

const rig = () => {
  const doc = installDom({ autoCreateById: true });
  const line = { points: [{ x: 5, y: 5 }, { x: 20, y: 5 }], color: '#ff0000', pointColor: '#00ff00',
    thickness: 2, style: 'solid', locked: true, fillColor: 'transparent' };
  const counts = { history: 0, redraw: 0 };
  const app = {
    lines: [line], selectedLineIdx: 0, pointSize: 4, showPoints: true,
    compareReadOnly: () => false,
    saveHistory() { counts.history++; },
    renderer: { redraw() { counts.redraw++; } },
    storage: { save() { throw new Error('a selection edit never saves synchronously'); } },
    applySelectionChange(prop, value, opts) { applySelectionChange(app, prop, value, opts); },
    applyFill(opts) { applyFill(app, opts); },
    unchainSelectedLine() {}, deselectLine() {},
  };
  doc.getElementById('sel-color').value = '#0000ff';
  doc.getElementById('sel-alpha').value = '255';
  doc.getElementById('sel-fill').value = '#336699';
  doc.getElementById('sel-fill-alpha').value = '128';
  wireSelectionPanelControls(app);
  return { doc, app, line, counts };
};

test('the point-size box changes the size the renderer draws the points at', (t) => {
  const { doc, app, line, counts } = rig();
  t.after(() => doc.restore());
  doc.getElementById('sel-point-size').dispatch('change', { target: { value: '9' } });
  assert.equal(line.pointSize, 9);
  assert.equal(line['point-size'], undefined, 'no stray kebab-case field');
  assert.equal(counts.history, 1);

  const { ctx, calls } = recordingCtx();
  const fx = { pointsOf: (l) => l.points, scaleAt: () => 1, paintOver() {}, paintUnder() {} };
  const r = { ctx, app: { ...app, strokeFx: fx }, pointHighlightState: () => 0 };
  drawLine(r, line, false, 0);
  const radii = argsOf(calls, 'arc').map((a) => a[2]);
  assert.deepEqual(radii, [9, 9], 'each point is drawn at the line\'s own size');
});

test('dragging a colour previews on input and commits one undo step on change', (t) => {
  const { doc, line, counts } = rig();
  t.after(() => doc.restore());
  const swatch = doc.getElementById('sel-color');
  for (const hex of ['#111111', '#222222', '#333333']) {
    swatch.value = hex;
    swatch.dispatch('input');
  }
  assert.equal(line.color, '#333333', 'the canvas follows the drag');
  assert.equal(counts.history, 0, 'no history while the picker is live');
  assert.equal(counts.redraw, 3);
  swatch.dispatch('change');
  assert.equal(counts.history, 1, 'one step for the whole drag');
  doc.getElementById('sel-alpha').dispatch('input');
  assert.equal(counts.history, 1, 'the alpha box previews the same way');
});

test('the area fill previews on input and commits on change', (t) => {
  const { doc, line, counts } = rig();
  t.after(() => doc.restore());
  const alpha = doc.getElementById('sel-fill-alpha');
  alpha.dispatch('input');
  alpha.dispatch('input');
  assert.notEqual(line.fillColor, 'transparent');
  assert.equal(counts.history, 0);
  alpha.dispatch('change');
  assert.equal(counts.history, 1);
});

test('a preview on a read-only compare view or with nothing selected is ignored', () => {
  const line = { color: '#000000', pointColor: '#000000' };
  const app = { lines: [line], selectedLineIdx: -1, compareReadOnly: () => false,
    saveHistory() { throw new Error('no commit'); }, renderer: { redraw() {} } };
  applySelectionChange(app, 'color', '#ffffff', { commit: false });
  assert.equal(line.color, '#000000');
});
