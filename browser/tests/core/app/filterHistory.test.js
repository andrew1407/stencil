// The filter as an undo step (core/settings/filterStep.js under core/app/editing.js): a committed
// mode or tint is one step, a preview or a no-op none, and undo and redo put the filter back
// through the toolbar's own setter without pushing. A real DrawingApp prototype over stubs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

const doc = installDom({ autoCreateById: true }, {
  location: { hash: '', pathname: '/', search: '' }, history: { replaceState: () => {} },
});
const { DrawingApp } = await import('../../../js/core/drawingApp.js');
const { createEditorState } = await import('../../../js/core/editorState.js');
const { HistoryStack, editorMemento } = await import('../../../js/core/historyStack.js');
const { SettingsController } = await import('../../../js/core/settings/controller.js');
const { createSettingsFacade } = await import('../../../js/console/settingsFacade.js');
const { createEditorActions } = await import('../../../js/console/editorActions.js');

const line = (y) => ({ points: [{ x: 10, y }, { x: 60, y }], color: '#ff0000' });

const makeApp = ({ image = { width: 100, height: 80 }, imageFilter = 'none', lines = [] } = {}) => {
  const rec = { saves: 0, syncs: 0 };
  const app = Object.create(DrawingApp.prototype);
  Object.assign(app, createEditorState(), {
    image, imageFilter, lines, cropRect: { x: 0, y: 0, width: 100, height: 80 }, rotationQuarters: 0,
    history: new HistoryStack(),
    renderer: { redraw() {}, effectiveCompareMode: () => 'none' },
    storage: { saveSoon() { rec.saves++; } },
    remoteSync: { scheduleRemoteSync() { rec.syncs++; } },
    coordTable: { update() {} },
    strokeFx: { cancel() {} },
    imageModel: { restoreView: () => false },
    rec,
  });
  app.settings = new SettingsController(app);
  app.history.reset(editorMemento(app));
  return app;
};
const steps = (app) => app.history.history.length;

test('a mode switch is one step; undo and redo set the filter back without pushing', () => {
  const app = makeApp();
  app.settings.setImageFilter('sepia');
  assert.equal(steps(app), 1);
  assert.equal(app.history.canUndo(), true);
  app.filterDirty = false;
  app.undo();
  assert.equal(app.imageFilter, 'none');
  assert.equal(doc.getElementById('image-filter').value, 'none', 'the control follows');
  assert.equal(app.filterDirty, true, 'an undone filter is this editor\'s to push');
  assert.equal(steps(app), 1, 'undo pushed nothing');
  app.redo();
  assert.equal(app.imageFilter, 'sepia');
  assert.equal(steps(app), 1);
  assert.equal(app.history.canRedo(), false);
});

test('a commit that leaves the mode and tint as they are pushes nothing', () => {
  const app = makeApp();
  app.settings.setImageFilter('none');
  app.settings.setFilterColor(app.filterColor);
  app.settings.setFilter({ filter: 'none' });
  assert.equal(steps(app), 0);
  assert.equal(app.history.canUndo(), false);
});

test('a tint drag previews without a step and its release commits one', () => {
  const app = makeApp({ imageFilter: 'custom' });
  const was = app.filterColor;
  for (const c of ['#110000', '#220000', '#330000']) app.settings.setFilterColor(c, { persist: false });
  assert.equal(steps(app), 0, 'no step per input');
  app.settings.setFilterColor('#330000');
  assert.equal(steps(app), 1);
  app.undo();
  assert.equal(app.filterColor, was);
});

test('mode and tint together are one step, and so are the commits of one batch', () => {
  const app = makeApp();
  app.settings.setFilter({ filter: 'custom', filterColor: '#ff0000' });
  assert.equal(steps(app), 1);
  app.settings.filterStep(() => { app.settings.setImageFilter('bw'); app.settings.setFilterColor('#00ff00'); });
  assert.equal(steps(app), 2);
  app.undo();
  assert.deepEqual([app.imageFilter, app.filterColor], ['custom', '#ff0000']);
  app.settings.setFilter({ filter: 'sepia' }, { history: false });
  assert.equal(app.history.historyStep, 0, 'history: false records nothing');
});

test('the facade\'s bulk apply lands a filter and its tint as one step', () => {
  const app = makeApp();
  const { settings } = createSettingsFacade({ app, guard: (o) => o });
  const { api, setFacade } = createEditorActions({ app });
  setFacade({ get settings() { return settings(); } });
  api.apply({ filter: 'custom', filterColor: '#123456' });
  assert.equal(steps(app), 1);
  assert.deepEqual([app.imageFilter, app.filterColor], ['custom', '#123456']);
});

test('a stroke carries the filter it was drawn over, and the floor keeps the loaded one', () => {
  const app = makeApp({ imageFilter: 'sepia', lines: [line(10)] });
  app.history.reset(editorMemento(app), 0);
  app.settings.setImageFilter('bw');
  app.lines = [line(10), line(20)];
  app.saveHistory();
  app.undo();
  assert.deepEqual([app.lines.length, app.imageFilter], [1, 'bw']);
  app.undo();
  assert.deepEqual([app.lines.length, app.imageFilter], [1, 'sepia']);
  app.undo();
  assert.deepEqual([app.lines.length, app.imageFilter], [0, 'sepia'], 'the floor: no lines, the loaded filter');
});

test('a step without a filter leaves it as it is, and a restore never pushes', () => {
  const app = makeApp({ imageFilter: 'bw' });
  app.restoreHistoryStep([line(5)]);
  app.restoreHistoryStep({ lines: [], cropRect: null, rotationQuarters: 0, filter: '' });
  assert.equal(app.imageFilter, 'bw');
  app.restoreHistoryStep({ lines: [], cropRect: null, rotationQuarters: 0, filter: 'sepia', filterColor: '#7c3aed' });
  assert.equal(app.imageFilter, 'sepia');
  assert.equal(steps(app), 0);
});

test('with no picture a filter change is a setting, not a step', () => {
  const app = makeApp({ image: null });
  app.settings.setImageFilter('bw');
  assert.equal(app.imageFilter, 'bw');
  assert.equal(app.history.canUndo(), false);
});
