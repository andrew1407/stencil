// SettingsController.wireFormulaInputs: the f(x,y) fields commit on an idle pause (or Enter/blur),
// judged by the real FormulaEngine. Split from settingsController.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SettingsController, makeApp } from './helpers/settingsControllerRig.js';

const { FormulaEngine } = await import('../js/core/formulaEngine.js');
const { COMMIT_DEBOUNCE_MS } = await import('../js/ui/control/numericInput.js');

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
