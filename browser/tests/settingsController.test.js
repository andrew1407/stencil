// Unit tests for SettingsController (js/core/settingsController.js) — the shared editor
// setters extracted out of DrawingApp. The controller mutates the back-referenced `app`
// model, mirrors the DOM (tolerant of missing elements), and routes persistence/redraw/sync
// through the app. We stub document (all lookups → null) and drive it with a fake app that
// records the shared-method calls, asserting each setter updates the model and side-effects.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// The setters call document.getElementById for UI mirroring; return null so the guards
// short-circuit (setVal/setRadioGroup already no-op on a missing element).
globalThis.document = globalThis.document || {};
globalThis.document.getElementById = () => null;
globalThis.document.querySelectorAll = () => [];
globalThis.document.activeElement = null;

const { SettingsController } = await import('../js/core/settingsController.js');

const makeApp = (over = {}) => {
  const rec = { save: 0, redraw: 0, remoteSync: 0, coordUpdate: 0 };
  return {
    rec,
    color: '#000', thickness: 1, markerSize: 1, style: 'solid',
    showPoints: true, showLines: true, imageFilter: 'none', filterColor: '#7c3aed',
    pageSize: 'A3', customPageWidth: 21, customPageHeight: 29.7, unit: 'cm',
    allowFormulas: false, formulaX: '', formulaY: '', filterDirty: false,
    coordLineIdx: -1, currentLine: null, lines: [],
    renderer: { redraw() { rec.redraw++; } },
    storage: { save() { rec.save++; } },
    coordTable: { update() { rec.coordUpdate++; } },
    scheduleRemoteSync() { rec.remoteSync++; },
    applyUnitToUI() {}, updateCoordStatus() {},
    formula: { validate: (v) => v !== 'bad' },
    tooltipMgr: { refresh() {} },
    ...over,
  };
};

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
  assert.equal(app.thickness, 7); // unchanged
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
  assert.throws(() => s.setVisualColor('nope', '#fff'), /Unknown visual colour/);
});

test('setVisualColor: known key updates the mapped model field', () => {
  const app = makeApp();
  new SettingsController(app).setVisualColor('selGlow', '#123456');
  assert.equal(app.selGlowColor, '#123456');
  assert.equal(app.rec.redraw, 1);
});
