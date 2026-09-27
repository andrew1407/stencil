// A peer's edit on the picture on screen (core/remote/peerLayout.js) is one undo step when its
// lines or its filter moved, and none for a result upload; undoing a later stroke of this editor's
// then keeps the peer's filter rather than reverting it and pushing that back.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

installDom({ autoCreateById: true }, {
  location: { hash: '', pathname: '/', search: '' }, history: { replaceState: () => {} },
});
const { DrawingApp } = await import('../../../js/core/drawingApp.js');
const { createEditorState } = await import('../../../js/core/editorState.js');
const { HistoryStack, editorMemento } = await import('../../../js/core/historyStack.js');
const { SettingsController } = await import('../../../js/core/settings/controller.js');
const { ImageModel } = await import('../../../js/core/image/model.js');
const { applyPeerLayout } = await import('../../../js/core/remote/peerLayout.js');
const { adoptServerFilter } = await import('../../../js/ui/canvas/serverLayoutPaint.js');

const line = (y) => ({ points: [{ x: 10, y }, { x: 60, y }], color: '#ff0000' });

const makeApp = () => {
  const app = Object.create(DrawingApp.prototype);
  Object.assign(app, createEditorState(), {
    image: { width: 100, height: 80 }, originalImage: { width: 100, height: 80 }, canvas: { width: 100, height: 80 }, lines: [line(10)],
    cropRect: { x: 0, y: 0, width: 100, height: 80 }, rotationQuarters: 0, history: new HistoryStack(),
    renderer: { redraw() {}, effectiveCompareMode: () => 'none' },
    storage: { saveSoon() {} }, remoteSync: { scheduleRemoteSync() {} },
    coordTable: { update() {} }, strokeFx: { cancel() {} },
  });
  app.imageModel = new ImageModel(app);
  app.settings = new SettingsController(app);
  app.history.reset(editorMemento(app));
  return app;
};
const remoteOf = (app) => ({
  adoptServerPageFormat() {}, adoptServerFormulas() {}, adoptServerFilter: (l) => adoptServerFilter(app, l),
});
const layoutOf = (app, over = {}) => ({
  imageWidth: 100, imageHeight: 80, lines: app.lines, imageFilter: app.imageFilter, filterColor: app.filterColor,
  cropRect: { x: 0, y: 0, w: 100, h: 80 }, rotationQuarters: 0, ...over,
});

test('a peer\'s filter-only edit is one step; the same layout again is none', () => {
  const app = makeApp();
  const before = app.history.history.length;
  assert.equal(applyPeerLayout(app, layoutOf(app, { imageFilter: 'sepia' }), remoteOf(app)), true);
  assert.equal(app.imageFilter, 'sepia');
  assert.equal(app.history.history.length, before + 1);
  applyPeerLayout(app, layoutOf(app), remoteOf(app));
  assert.equal(app.history.history.length, before + 1, 'a result upload leaves no empty step');
});

test('undoing a stroke drawn after a peer\'s filter keeps that filter', () => {
  const app = makeApp();
  applyPeerLayout(app, layoutOf(app, { imageFilter: 'bw' }), remoteOf(app));
  app.lines = [line(10), line(20)];
  app.saveHistory();
  app.filterDirty = false;
  app.undo();
  assert.equal(app.lines.length, 1);
  assert.equal(app.imageFilter, 'bw');
  assert.equal(app.filterDirty, false, 'nothing to push over the peer\'s filter');
  app.undo();
  assert.equal(app.imageFilter, 'none', 'the peer\'s edit is undone as its own step');
});

test('a peer\'s lines and filter in one edit are one step', () => {
  const app = makeApp();
  applyPeerLayout(app, layoutOf(app, { lines: [line(10), line(30)], imageFilter: 'custom', filterColor: '#00ff00' }),
    remoteOf(app));
  assert.equal(app.history.history.length, 2);
  app.undo();
  assert.deepEqual([app.lines.length, app.imageFilter], [1, 'none']);
});
