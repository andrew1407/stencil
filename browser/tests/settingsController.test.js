// SettingsController (js/core/settingsController.js): the direct setters — colour, thickness,
// filter, page size, formula text and the visual-colour map. Rig: helpers/settingsControllerRig.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SettingsController, makeApp } from './helpers/settingsControllerRig.js';


test('setColor: updates model + persists', () => {
  const app = makeApp();
  new SettingsController(app).setColor('#ff0000');
  assert.equal(app.color, '#ff0000');
  assert.equal(app.rec.save, 1);
});

test('setColor persist:false skips the write', () => {
  const app = makeApp();
  new SettingsController(app).setColor('#abc', { persist: false });
  assert.equal(app.color, '#abc');
  assert.equal(app.rec.save, 0);
});

test('setThickness: parses int, redraws, persists; NaN is a no-op', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setThickness('7');
  assert.equal(app.thickness, 7);
  assert.equal(app.rec.redraw, 1);
  s.setThickness('not-a-number');
  assert.equal(app.thickness, 7);
});

test('setImageFilter: marks filterDirty and schedules a remote sync', () => {
  const app = makeApp();
  new SettingsController(app).setImageFilter('bw');
  assert.equal(app.imageFilter, 'bw');
  assert.equal(app.filterDirty, true);
  assert.equal(app.rec.remoteSync, 1);
});

test('setPageSize: unknown format throws; a named format is applied + synced', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  assert.throws(() => s.setPageSize('ZZ9'), /Unknown page size/);
  s.setPageSize('a4');
  assert.equal(app.pageSize, 'A4');
  assert.equal(app.rec.remoteSync, 1);
});

test('setFormula: valid expr stored; invalid throws and leaves the model', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setFormula('x', 'x+1');
  assert.equal(app.formulaX, 'x+1');
  assert.throws(() => s.setFormula('y', 'bad'), /Invalid y formula/);
  assert.equal(app.formulaY, '');
});

test('setTooltipOption / setVisualColor: unknown keys throw', () => {
  const s = new SettingsController(makeApp());
  assert.throws(() => s.setTooltipOption('nope', true), /Unknown tooltip option/);
  assert.throws(() => s.setVisualColor('nope', '#fff'), /Unknown visual color/);
});

test('setVisualColor: known key updates the mapped model field', () => {
  const app = makeApp();
  new SettingsController(app).setVisualColor('selGlow', '#123456');
  assert.equal(app.selGlowColor, '#123456');
  assert.equal(app.rec.redraw, 1);
});

