// A peer's crop or turn over the original on screen (core/remote/peerLayout.js) lands in place: the
// view is re-derived from the in-memory original as undoing a crop re-derives it, with the peer's
// lines, as one undo step; the history before it survives, and nothing is pushed back.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

const doc = installDom({ autoCreateById: true }, {
  location: { hash: '', pathname: '/', search: '' }, history: { replaceState: () => {} },
});
let rebuilds = 0;
doc.createElement = (tag) => {
  const el = createStubElement(tag);
  if (tag === 'canvas') { rebuilds++; el.getContext = () => new Proxy({}, { get: () => () => {} }); }
  return el;
};
const { DrawingApp } = await import('../../../js/core/drawingApp.js');
const { createEditorState } = await import('../../../js/core/editorState.js');
const { HistoryStack, editorMemento } = await import('../../../js/core/historyStack.js');
const { SettingsController } = await import('../../../js/core/settings/controller.js');
const { ImageModel } = await import('../../../js/core/image/model.js');
const { applyPeerLayout } = await import('../../../js/core/remote/peerLayout.js');

const line = (y) => ({ points: [{ x: 10, y }, { x: 60, y }], color: '#ff0000' });

const makeApp = () => {
  const app = Object.create(DrawingApp.prototype);
  const pushes = [];
  Object.assign(app, createEditorState(), {
    originalImage: { width: 200, height: 100 }, image: {}, canvas: { width: 160, height: 100 }, lines: [line(10)],
    cropRect: { x: 20, y: 0, width: 160, height: 100 }, rotationQuarters: 0, history: new HistoryStack(),
    renderer: { redraw() {}, effectiveCompareMode: () => 'none' }, pushes,
    storage: { saveSoon() {} }, remoteSync: { scheduleRemoteSync: () => pushes.push('sync') },
    coordTable: { update() {} }, strokeFx: { cancel() {} }, zoomPan: { fitToWindow() {} },
    hideSelectionPanels() {}, updateInfo() {}, updateButtons() {}, updateCoordStatus() {},
  });
  app.imageModel = new ImageModel(app);
  app.settings = new SettingsController(app);
  app.history.reset(editorMemento(app));
  return app;
};
const remote = { adoptServerPageFormat() {}, adoptServerFormulas() {}, adoptServerFilter() {} };
const layoutOf = (app, over = {}) => ({
  imageWidth: app.canvas.width, imageHeight: app.canvas.height, lines: app.lines, imageFilter: 'none',
  cropRect: { x: app.cropRect.x, y: app.cropRect.y, w: app.cropRect.width, h: app.cropRect.height },
  rotationQuarters: app.rotationQuarters, ...over,
});

test('a peer\'s crop is one step, rebuilt from the original; undo walks back through it into history', () => {
  const app = makeApp();
  app.lines = [line(10), line(20)];
  app.saveHistory();
  const steps = app.history.history.length;
  rebuilds = 0;
  app.pushes.length = 0;   // this editor's own stroke syncs; the peer's view must not
  const ok = applyPeerLayout(app, layoutOf(app, { cropRect: { x: 0, y: 0, w: 100, h: 100 }, lines: [line(5)] }), remote);
  assert.equal(ok, true, 'adopted in place, not reloaded');
  assert.deepEqual([app.cropRect, app.canvas.width, rebuilds], [{ x: 0, y: 0, width: 100, height: 100 }, 100, 1]);
  assert.deepEqual([app.lines.length, app.history.history.length], [1, steps + 1]);
  assert.deepEqual(app.pushes, [], 'the server already holds the peer\'s view');
  applyPeerLayout(app, layoutOf(app), remote);
  assert.equal(app.history.history.length, steps + 1, 'a result upload leaves no empty step');
  app.undo();
  assert.deepEqual([app.cropRect.x, app.cropRect.width, app.lines.length], [20, 160, 2]);
  app.undo();
  assert.deepEqual([app.cropRect.width, app.lines.length], [160, 1], 'this editor\'s own step survived');
});

test('a peer\'s quarter turn lands in the rotated original\'s space', () => {
  const app = makeApp();
  const turned = layoutOf(app, { rotationQuarters: 1, cropRect: { x: 0, y: 40, w: 100, h: 120 } });
  assert.equal(applyPeerLayout(app, turned, remote), true);
  assert.deepEqual([app.rotationQuarters, app.cropRect, app.canvas.height], [1, { x: 0, y: 40, width: 100, height: 120 }, 120]);
  assert.equal(app.history.history.length, 2);
  app.undo();
  assert.deepEqual([app.rotationQuarters, app.cropRect.width], [0, 160]);
});

test('a layout that names no crop rect is left to the reload', () => {
  const app = makeApp();
  assert.equal(applyPeerLayout(app, layoutOf(app, { cropRect: undefined }), remote), false);
  assert.deepEqual([app.cropRect.width, app.history.history.length], [160, 1]);
});
