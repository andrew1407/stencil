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
    remoteSync: { scheduleRemoteSync() { rec.remoteSync++; } },
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
  assert.throws(() => s.setVisualColor('nope', '#fff'), /Unknown visual colour/);
});

test('setVisualColor: known key updates the mapped model field', () => {
  const app = makeApp();
  new SettingsController(app).setVisualColor('selGlow', '#123456');
  assert.equal(app.selGlowColor, '#123456');
  assert.equal(app.rec.redraw, 1);
});

// ── registry-driven set() (backs setColor/setThickness/… ) ──
test('set(): unknown key throws', () => {
  const s = new SettingsController(makeApp());
  assert.throws(() => s.set('nope', 1), /Unknown setting/);
});

test('setMarkerSize: parses int, redraws, persists; NaN aborts before mutating', () => {
  const app = makeApp();
  const s = new SettingsController(app);
  s.setMarkerSize('9');
  assert.equal(app.markerSize, 9);
  assert.equal(app.rec.redraw, 1);
  assert.equal(app.rec.save, 1);
  s.setMarkerSize('nan');
  assert.equal(app.markerSize, 9);
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
