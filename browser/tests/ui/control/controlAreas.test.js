// The control areas follow the editor's change feed (ui/control/state.js over core/app/changes.js):
// a drag, a selection change, an undo or a compare peek repaints only the areas whose inputs it
// moved, and each narrow flush leaves every control exactly where a full sweep would.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

const doc = installDom({ autoCreateById: true }, {
  location: { hash: '', pathname: '/', search: '' }, history: { replaceState: () => {} },
});
// The tooltip pass reads the page's tipped controls; every gated one carries a reason here.
doc.querySelectorAll = () => [...doc.els.values()].filter((el) => el.dataset.disabledReason || el.dataset.hkTitle);

const { DrawingApp } = await import('../../../js/core/drawingApp.js');
const { createEditorState } = await import('../../../js/core/editorState.js');
const { HistoryStack } = await import('../../../js/core/historyStack.js');
const { Emitter } = await import('../../../js/core/emitter.js');
const { CHANGE, changed } = await import('../../../js/core/app/changes.js');
const { AREAS, updateButtons, onButtonsUpdated, wireControlState } = await import('../../../js/ui/control/state.js');
const { beginSegmentDrag, dragMove, endSegmentDrag } = await import('../../../js/core/touch/dragGestures.js');
const { selectLineFromList } = await import('../../../js/core/line/selection.js');

const GATED = ['undo', 'redo', 'draw-toggle', 'crop-image', 'save-image', 'download-json', 'clear-all-lines',
  'description-btn', 'incognito-toggle', 'zoom-in', 'compare-mode'];
for (const id of GATED) doc.getElementById(id).dataset.disabledReason = `no ${id}`;

const line = (y) => ({ points: [{ x: 10, y }, { x: 60, y }], color: '#ff0000', thickness: 2, pointSize: 4, style: 'solid' });

// A real DrawingApp prototype (view seam, editing mixin, gesture flags) over plain collaborators.
const makeApp = () => {
  const app = Object.create(DrawingApp.prototype);
  Object.assign(app, createEditorState(), {
    image: { width: 100, height: 80 },
    canvas: createStubElement('canvas', {
      width: 100, height: 80, getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 80 }),
    }),
    lines: [line(20), line(40)],
    activeProjectId: 'p1', remoteLink: null, compareMode: 'none',
    openInConfig: { desktopScheme: '', telegramBotUsername: '' },
    changes: new Emitter(),
    history: new HistoryStack(),
    renderer: { redraw() {}, requestRedraw() {}, effectiveCompareMode: () => app.compareMode },
    storage: { incognito: false, temporary: false, saveSoon() {}, save() {}, store: { getMeta: () => ({ name: 'Plan' }) } },
    remoteSync: { scheduleRemoteSync() {} },
    coordTable: { update() {}, refreshCoordRow() {}, refreshRows() {} },
    strokeFx: { cancel() {} },
    imageModel: { restoreView: () => false },
    stencilSync: { supported: false, linked: false, liveSync: false, name: '' },
    tabs: { reportIncognito() {} },
    showSelectionPanel() {},
  });
  wireControlState(app);
  app.saveHistory();
  updateButtons(app);
  return app;
};

// Which areas each flush ran, in order.
const record = () => {
  const ran = [];
  const off = onButtonsUpdated((names) => ran.push(names));
  return { ran, off };
};

const snapshot = () => {
  const out = { body: [...doc.body.classes].sort() };
  for (const [id, el] of doc.els) {
    out[id] = { disabled: el.disabled, display: el.style.display, cls: [...el.classes].sort(), tip: el.dataset.tip,
      title: el.dataset.title, face: el.innerHTML };
  }
  return out;
};

// The narrow flush's result, then a full sweep over it: the sweep must find nothing to change.
const sameAsFullSweep = (app, what) => {
  const narrow = snapshot();
  updateButtons(app);
  assert.deepEqual(snapshot(), narrow, `${what}: a full sweep would leave some control elsewhere`);
};

test('a drag moves no control until it lands, then only the undo pair', () => {
  const app = makeApp();
  const { ran, off } = record();
  beginSegmentDrag(app, { lineIdx: 0, ptIdx1: 0, ptIdx2: 1 }, 10, 20);
  for (const x of [12, 16, 24]) dragMove(app, x, 30, false);
  assert.deepEqual(ran, [], 'pointer moves repaint no control');
  endSegmentDrag(app, false);
  off();
  assert.deepEqual(ran, [['history']]);
  assert.equal(doc.getElementById('undo').disabled, false, 'the drag is one undoable step');
  sameAsFullSweep(app, 'a drag');
});

test('a selection change repaints the lines list alone', () => {
  const app = makeApp();
  const { ran, off } = record();
  selectLineFromList(app, 1);
  off();
  assert.equal(app.selectedLineIdx, 1);
  assert.deepEqual(ran, [['list']]);
  sameAsFullSweep(app, 'a selection change');
});

test('an undo repaints the undo pair, the line gates, the project row and the list — no other area', () => {
  const app = makeApp();
  app.lines = [...app.lines, line(60)];
  app.saveHistory();
  const { ran, off } = record();
  app.undo();
  off();
  assert.equal(app.lines.length, 2, 'the undo really restored the step');
  assert.deepEqual(ran, [['history', 'project', 'lines', 'list']]);
  assert.equal(doc.getElementById('redo').disabled, false);
  sameAsFullSweep(app, 'an undo');
});

test('a compare peek greys the editing areas, and leaves the image and project areas alone', () => {
  const app = makeApp();
  const { ran, off } = record();
  app.compareMode = 'original';
  changed(app, CHANGE.compare);
  off();
  assert.deepEqual(ran, [['history', 'canvas', 'draw', 'lines']]);
  assert.equal(doc.getElementById('clear-all-lines').disabled, true, 'a comparison cannot be edited');
  assert.ok(doc.body.classes.has('canvas-readonly'));
  sameAsFullSweep(app, 'a compare peek');
});

test('one signal naming several channels runs each area once; the full sweep runs them all', () => {
  const app = makeApp();
  const { ran, off } = record();
  changed(app, CHANGE.lines, CHANGE.selection, CHANGE.lines);
  updateButtons(app);
  off();
  assert.deepEqual(ran, [['project', 'lines', 'list'], AREAS.map((a) => a.name)]);
});
