// Unit tests for SettingsController (js/core/settingsController.js) — the shared editor
// setters extracted out of DrawingApp. The controller mutates the back-referenced `app`
// model, mirrors the DOM (tolerant of missing elements), and routes persistence/redraw/sync
// through the app. We stub document (all lookups → null) and drive it with a stub app that
// records the shared-method calls, asserting each setter updates the model and side-effects.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// The setters call document.getElementById for UI mirroring; the stub document returns
// null for every unregistered id, so the guards short-circuit (setVal/setRadioGroup
// already no-op on a missing element).
import { installDom } from './helpers/dom.js';

installDom();

const { SettingsController } = await import('../js/core/settingsController.js');

const makeApp = (over = {}) => {
  const rec = { save: 0, redraw: 0, remoteSync: 0, coordUpdate: 0 };
  return {
    rec,
    color: '#000', thickness: 1, pointSize: 1, style: 'solid',
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
  assert.throws(() => s.setVisualColor('nope', '#fff'), /Unknown visual color/);
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

// ── wireFormulaInputs: the f(x,y) fields commit on an idle pause ─────────────
// A formula is typed one character at a time, so the pair applies only once typing
// settles (or on Enter/blur): "(x" and a field cleared to be retyped are states passed
// THROUGH, and must neither reach the model nor flash the invalid indicator. The real
// FormulaEngine judges validity here — a stub one would prove nothing about the wiring.

const { FormulaEngine } = await import('../js/core/formulaEngine.js');
const { COMMIT_DEBOUNCE_MS } = await import('../js/ui/numericInput.js');

const stubEl = () => {
  const listeners = new Map();
  const el = {
    value: '', style: {},
    addEventListener(t, fn) {
      if (!listeners.has(t)) listeners.set(t, []);
      listeners.get(t).push(fn);
    },
    fire(t, extra = {}) {
      for (const fn of listeners.get(t) || []) fn({ type: t, preventDefault() {}, ...extra });
    },
    type_(text) { el.value = text; el.fire('input'); },
  };
  return el;
};

// Swap the null-returning getElementById stub for a registry of stub formula elements.
const withFormulaDom = (fn) => {
  const ids = ['formula-x', 'formula-y', 'ctx-formula-x', 'ctx-formula-y',
               'formula-error', 'ctx-formula-error'];
  const els = new Map(ids.map(id => [id, stubEl()]));
  const prev = globalThis.document.getElementById;
  globalThis.document.getElementById = id => els.get(id) || null;
  try {
    const app = makeApp({ formula: new FormulaEngine() });
    const s = new SettingsController(app);
    s.wireFormulaInputs({
      x: 'formula-x', y: 'formula-y', mirrorX: 'ctx-formula-x', mirrorY: 'ctx-formula-y',
    });
    return fn({ app, els, errorShown: () => els.get('formula-error').style.display === 'inline' });
  } finally {
    globalThis.document.getElementById = prev;
  }
};

test('wireFormulaInputs: a half-written formula is neither applied nor flagged', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withFormulaDom(({ app, els, errorShown }) => {
    els.get('formula-x').type_('(x + ');
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS - 1);
    assert.equal(app.formulaX, '', 'the model never saw the partial expression');
    assert.equal(errorShown(), false, 'no red flag while the user is still typing');
    assert.equal(app.rec.save, 0);
    assert.equal(app.rec.remoteSync, 0);
    // Settle on it anyway → now it is worth flagging, and still nothing is applied.
    t.mock.timers.tick(1);
    assert.equal(errorShown(), true);
    assert.equal(app.formulaX, '');
  });
});

test('wireFormulaInputs: one commit per settle, not one per keystroke', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withFormulaDom(({ app, els, errorShown }) => {
    for (const text of ['x', 'x*', 'x*2', 'x*2+', 'x*2+1']) {
      els.get('formula-x').type_(text);
      t.mock.timers.tick(50);                    // typing, no pause long enough to commit
    }
    assert.equal(app.formulaX, '', 'nothing applied while the keystrokes kept coming');
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
    assert.equal(app.formulaX, 'x*2+1');
    assert.equal(errorShown(), false);
    assert.equal(app.rec.save, 1, 'one storage write for the whole expression');
    assert.equal(app.rec.remoteSync, 1, 'one peer sync, not one per character');
    assert.equal(app.rec.coordUpdate, 1);
    // The committed value is mirrored into the context-menu twin.
    assert.equal(els.get('ctx-formula-x').value, 'x*2+1');
  });
});

test('wireFormulaInputs: clearing to retype does not reset the transform mid-edit', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withFormulaDom(({ app, els }) => {
    els.get('formula-x').type_('x*2');
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
    assert.equal(app.formulaX, 'x*2');

    els.get('formula-x').type_('');            // select-all + delete, then retype
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS - 1);
    assert.equal(app.formulaX, 'x*2', 'the transform held while the field was empty');
    els.get('formula-x').type_('x*3');
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
    assert.equal(app.formulaX, 'x*3');
    assert.equal(app.rec.save, 2, 'two commits total — no identity reset in between');
  });
});

test('wireFormulaInputs: a settled empty field still clears the formula', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withFormulaDom(({ app, els }) => {
    els.get('formula-x').type_('x*2');
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
    els.get('formula-x').type_('');
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
    assert.equal(app.formulaX, '', 'clearing and walking away removes the formula');
  });
});

test('wireFormulaInputs: Enter and blur apply at once, no waiting', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withFormulaDom(({ app, els }) => {
    els.get('formula-x').type_('x+1');
    els.get('formula-x').fire('keydown', { key: 'Enter' });
    assert.equal(app.formulaX, 'x+1');
    els.get('formula-y').type_('y-2');
    els.get('formula-y').fire('blur');
    assert.equal(app.formulaY, 'y-2');
    assert.equal(app.rec.save, 2);
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS);     // the cancelled timers must not re-commit
    assert.equal(app.rec.save, 2);
  });
});

test('wireFormulaInputs: fixing an invalid formula clears the error on the spot', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withFormulaDom(({ els, errorShown }) => {
    els.get('formula-x').type_('x*');
    t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
    assert.equal(errorShown(), true);
    els.get('formula-x').type_('x*2');          // no tick — the fix registers immediately
    assert.equal(errorShown(), false);
  });
});
