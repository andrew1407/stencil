// The logo's other targets (js/ui/drag/logoTargets.js): a Lines-tab row, the selected-line bar or a
// line under the pointer on the canvas takes the toolbar's style as one undo step; a control goes
// back to its default through its own reset; each glows only while the pointer is over it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SettingsController, makeApp } from '../../helpers/settingsControllerRig.js';
import { createStubElement } from '../../helpers/dom.js';

const { logoDragHooks } = await import('../../../js/ui/drag/logoDrag.js');
const { originalOf, resetDropped, sameTarget } = await import('../../../js/ui/drag/logoTargets.js');
const { applyToolbarStyle } = await import('../../../js/core/line/selection.js');
const { TARGET_CLASS, TARGET_OVER_CLASS } = await import('../../../js/ui/drag/iconDrag.js');

const TOOLBAR = { color: '#ffff00', pointColor: '', thickness: 2, pointSize: 4, style: 'solid' };
const line = () => ({ points: [{ x: 0, y: 0 }, { x: 9, y: 9 }], color: '#ff0000', pointColor: '#00ff00',
  thickness: 7, pointSize: 9, style: 'dashed' });
const styleOf = (l) => Object.fromEntries(Object.keys(TOOLBAR).map((k) => [k, l[k]]));

const rig = ({ hitLine = -1, control = null, ...over } = {}) => {
  const app = makeApp({ image: { width: 6, height: 4 }, imageFilter: 'sepia', ...TOOLBAR,
    lines: [line(), line()], selectedLineIdx: -1, listHoverLineIdx: -1, compareReadOnly: () => false, ...over });
  app.steps = 0;
  app.saveHistory = () => { app.steps++; };
  app.settings = new SettingsController(app);
  app.previews = [];
  app.renderer.previewClean = (on) => app.previews.push(on);
  app.shown = [];
  app.showSelectionPanel = (l) => app.shown.push(l);
  app.canvas = { width: 100, height: 100, style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 100 }) };
  app.findLineAt = () => hitLine;
  const canvas = createStubElement('canvas');
  const viewport = createStubElement('div', { contains: (n) => n === canvas });
  const resets = [];
  const hooks = logoDragHooks(app, { viewport: () => viewport, controlAt: (t) => (t === control ? control : null),
    reset: (el) => resets.push(el) });
  hooks.start({ event: {} });
  return { app, hooks, viewport, canvas, resets };
};

const rowFor = (idx) => {
  const row = createStubElement('tr');
  row.dataset.idx = String(idx);
  const cell = createStubElement('td', { closest: (sel) => (sel === 'tr.lines-row' ? row : null) });
  return { row, cell };
};

test('applyToolbarStyle: the line wears the toolbar style as one step; a second time records nothing', () => {
  const { app } = rig();
  assert.equal(applyToolbarStyle(app, 1), true);
  assert.deepEqual(styleOf(app.lines[1]), TOOLBAR);
  assert.equal(app.steps, 1);
  assert.equal(applyToolbarStyle(app, 1), false);
  assert.equal(app.steps, 1);
  assert.deepEqual(styleOf(app.lines[0]), styleOf(line()), 'no other line moves');
  const readOnly = rig({ compareReadOnly: () => true }).app;
  assert.equal(applyToolbarStyle(readOnly, 0), false);
});

test('dropped on a Lines-tab row, that line takes the toolbar style and the row glows only while over', () => {
  const { app, hooks } = rig();
  const { row, cell } = rowFor(1);
  hooks.move({ target: cell, x: 500, y: 500 });
  assert.ok(row.classes.has(TARGET_OVER_CLASS));
  assert.equal(app.listHoverLineIdx, 1, 'the line wears the list hover glow');
  hooks.drop({ target: cell, x: 500, y: 500 });
  assert.deepEqual(styleOf(app.lines[1]), TOOLBAR);
  assert.equal(app.imageFilter, 'sepia', 'no clean view');
  assert.ok(!row.classes.has(TARGET_CLASS));
  assert.equal(app.listHoverLineIdx, -1);
  assert.deepEqual(app.previews, []);
});

test('dropped on the selected-line bar, the selected line takes it and the bar re-shows it', () => {
  const { app, hooks } = rig({ selectedLineIdx: 0 });
  const bar = createStubElement('div');
  const field = createStubElement('input', { closest: (sel) => (sel.includes('#selection-panel') ? bar : null) });
  hooks.drop({ target: field, x: 0, y: 0 });
  assert.deepEqual(styleOf(app.lines[0]), TOOLBAR);
  assert.deepEqual(app.shown, [app.lines[0]]);
});

test('over a line on the canvas the line is the target, not the clean view', () => {
  const { app, hooks, viewport, canvas } = rig({ hitLine: 0 });
  hooks.move({ target: canvas, x: 5, y: 5 });
  assert.deepEqual(app.previews, [], 'no clean preview over a line');
  assert.equal(app.listHoverLineIdx, 0);
  assert.ok(viewport.classes.has(TARGET_OVER_CLASS));
  hooks.drop({ target: canvas, x: 5, y: 5 });
  assert.deepEqual(styleOf(app.lines[0]), TOOLBAR);
  assert.equal(app.imageFilter, 'sepia');
  assert.ok(!viewport.classes.has(TARGET_CLASS));
});

test('dropped on a control it resets through the given reset; the glow sits on its face while over', () => {
  const pill = createStubElement('span');
  const control = createStubElement('input', { closest: (sel) => (sel.includes('.pill-toggle') ? pill : null) });
  const { hooks, resets, app } = rig({ control });
  hooks.move({ target: control, x: 0, y: 0 });
  assert.ok(pill.classes.has(TARGET_OVER_CLASS));
  hooks.move({ target: createStubElement('div'), x: 0, y: 0 });
  assert.ok(!pill.classes.has(TARGET_CLASS), 'off it, the glow goes');
  hooks.move({ target: control, x: 0, y: 0 });
  hooks.drop({ target: control, x: 0, y: 0 });
  assert.deepEqual(resets, [control]);
  assert.equal(app.imageFilter, 'sepia');
  assert.ok(!pill.classes.has(TARGET_CLASS));
});

test('a fullscreen clone resets its original and then shows what the original holds', () => {
  const orig = createStubElement('input', { type: 'number', id: 'line-thickness', value: '9' });
  const doc = { querySelector: (sel) => (sel === '#controls-body #line-thickness' ? orig : null) };
  const clone = createStubElement('input', { type: 'number', id: 'line-thickness', value: '9', ownerDocument: doc,
    closest: (sel) => (sel === '#fs-controls-panel' ? {} : null) });
  assert.equal(originalOf(clone), orig);
  const plain = createStubElement('input', { id: 'x' });
  assert.equal(originalOf(plain), plain);
  orig.dispatchEvent = () => true;
  resetDropped(clone);
  assert.equal(orig.value, '2', 'the toolbar thickness default');
  assert.equal(clone.value, '2');
});

test('sameTarget tells two aims apart by kind, element and line', () => {
  const el = {};
  assert.ok(sameTarget(null, null));
  assert.ok(sameTarget({ kind: 'line', el, idx: 1 }, { kind: 'line', el, idx: 1 }));
  assert.ok(!sameTarget({ kind: 'line', el, idx: 1 }, { kind: 'line', el, idx: 2 }));
  assert.ok(!sameTarget({ kind: 'clean', el }, { kind: 'line', el, idx: 0 }));
});
