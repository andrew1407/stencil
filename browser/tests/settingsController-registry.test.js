// SettingsController's registry-driven set()/preview() (js/core/controller.js): the
// coercions, the persist:false path and the page/unit setters. From settingsController.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SettingsController, makeApp } from './helpers/settingsControllerRig.js';

// ── registry-driven set() (backs setColor/setThickness/… ) ──
test('set(): unknown key throws', () => {
  const s = new SettingsController(makeApp());
  assert.throws(() => s.set('nope', 1), /Unknown setting/);
});

test('setPointSize: parses int, redraws, persists; NaN aborts before mutating', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setPointSize('9');
  assert.equal(app.pointSize, 9);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 1);
  s.setPointSize('nan');
  assert.equal(app.pointSize, 9);
  assert.equal(app.rec.redraw, 1);   // no extra redraw/save on abort
  assert.equal(app.rec.save, 1);
});

test('setThickness persist:false redraws but skips the write', () => {
  const app = makeApp();
  new SettingsController(app).setThickness('5', { persist: false });
  assert.equal(app.thickness, 5);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 0);
});

test('setShowPoints / setShowLines: coerce to bool, redraw + persist', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setShowPoints(0);
  assert.equal(app.showPoints, false);
  s.setShowLines('yes');
  assert.equal(app.showLines, true);
  assert.equal(app.rec.redraw, 2);
  assert.equal(app.rec.save, 2);
});

test('setFilterColor: persist marks filterDirty + schedules sync; persist:false skips both', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setFilterColor('#abcdef');
  assert.equal(app.filterColor, '#abcdef');
  assert.equal(app.filterDirty, true);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 1);
  assert.equal(app.rec.remoteSync, 1);

  const app2 = makeApp();
  new SettingsController(app2).setFilterColor('#111111', { persist: false });
  assert.equal(app2.filterColor, '#111111');
  assert.equal(app2.filterDirty, false);
  assert.equal(app2.rec.redraw, 1);        // redraw still runs
  assert.equal(app2.rec.save, 0);
  assert.equal(app2.rec.remoteSync, 0);
});

// ── newly registry-migrated setters ──
test('setLineStyle: coerces to string, saves, does not redraw', () => {
  const app = makeApp();
  new SettingsController(app).setLineStyle('dashed');
  assert.equal(app.style, 'dashed');
  assert.equal(app.rec.save, 1);
  assert.equal(app.rec.redraw, 0);   // line style never repaints the canvas
  assert.equal(app.rec.remoteSync, 0);
});

test('setImageFilter: coerces, redraws, marks filterDirty, saves + syncs', () => {
  const app = makeApp();
  new SettingsController(app).setImageFilter('sepia');
  assert.equal(app.imageFilter, 'sepia');
  assert.equal(app.filterDirty, true);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 1);
  assert.equal(app.rec.remoteSync, 1);
});

test('preview: repaints the field WITHOUT persisting — no save/sync/filterDirty', () => {
  const app = makeApp();
  const ctrl = new SettingsController(app);
  ctrl.preview('imageFilter', 'sepia');
  assert.equal(app.imageFilter, 'sepia', 'the model field shows the previewed value');
  assert.equal(app.rec.redraw, 1, 'the canvas repaints');
  assert.equal(app.rec.save, 0, 'a preview never saves');
  assert.equal(app.rec.remoteSync, 0, 'a preview never syncs');
  assert.equal(app.filterDirty, false, 'a preview never dirties the filter');
  ctrl.preview('imageFilter', 'none');          // the "restore to committed" call
  assert.equal(app.imageFilter, 'none');
  assert.equal(app.rec.save, 0);
});

test('preview: a bad value is ignored, not thrown (compareMode parse rejects it)', () => {
  const app = makeApp();
  const before = app.compareMode;
  new SettingsController(app).preview('compareMode', 'not-a-mode');
  assert.equal(app.compareMode, before, 'the model is left as it was');
});

test('setPageSize: normalizes, updates coord table, redraws, saves + syncs', () => {
  const app = makeApp();
  new SettingsController(app).setPageSize('a5');
  assert.equal(app.pageSize, 'A5');
  assert.equal(app.rec.coordUpdate, 1);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 1);
  assert.equal(app.rec.remoteSync, 1);
});

test('setPageSize: invalid throws before mutating the model', () => {
  const app = makeApp();
  assert.throws(() => new SettingsController(app).setPageSize('nope'), /Unknown page size/);
  assert.equal(app.pageSize, 'A3');   // unchanged
  assert.equal(app.rec.save, 0);
});

test('setCustomPageWidth: NaN aborts; a number updates coord table, redraws, saves + syncs', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setCustomPageWidth('not-a-number');
  assert.equal(app.customPageWidth, 21);   // unchanged
  assert.equal(app.rec.save, 0);
  s.setCustomPageWidth('15.5');
  assert.equal(app.customPageWidth, 15.5);
  assert.equal(app.rec.coordUpdate, 1);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 1);
  assert.equal(app.rec.remoteSync, 1);
});

test('setCustomPageHeight: NaN aborts; a number updates coord table, redraws, saves + syncs', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setCustomPageHeight('nan');
  assert.equal(app.customPageHeight, 29.7);   // unchanged
  assert.equal(app.rec.save, 0);
  s.setCustomPageHeight('42');
  assert.equal(app.customPageHeight, 42);
  assert.equal(app.rec.coordUpdate, 1);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 1);
  assert.equal(app.rec.remoteSync, 1);
});

test('setUnit: only in/cm accepted; updates coord table, redraws, saves — no remote sync', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setUnit('in');
  assert.equal(app.unit, 'in');
  s.setUnit('parsecs');   // anything not 'in' → 'cm'
  assert.equal(app.unit, 'cm');
  assert.equal(app.rec.coordUpdate, 2);
  assert.equal(app.rec.redraw, 2);
  assert.equal(app.rec.save, 2);
  assert.equal(app.rec.remoteSync, 0);   // unit is a local display preference
});

test('setAllowFormulas: coerces to bool, refreshes coords, saves + syncs — no redraw', () => {
  const app = makeApp();
  new SettingsController(app).setAllowFormulas(1);
  assert.equal(app.allowFormulas, true);
  assert.equal(app.rec.coordUpdate, 1);   // refreshFormulaCoords → coordTable.update
  assert.equal(app.rec.redraw, 0);
  assert.equal(app.rec.save, 1);
  assert.equal(app.rec.remoteSync, 1);
});

