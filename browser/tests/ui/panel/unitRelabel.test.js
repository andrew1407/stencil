// Pins the shared page-format option-label contract (SPEC §1.3): pageFormatLabel
// renders in the requested display unit, and DrawingApp.applyUnitToUI re-labels the
// toolbar #page-size select so it doesn't stay frozen at the boot-time cm labels when
// the user switches units.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// Minimal DOM stubs so drawingApp.js (and its import graph) loads under node --test.
// Installed BEFORE the dynamic import; getElementById reads from the per-test `els` map.
import { installDom } from '../../helpers/dom.js';

const elements = installDom({}, {
  window: { dispatchEvent: () => {}, addEventListener: () => {} },
}).els;

const { DrawingApp } = await import('../../../js/core/drawingApp.js');
const { pageFormatLabel } = await import('../../../js/core/settings/units.js');
const constants = (await import('../../../js/config/constants.json', { with: { type: 'json' } })).default;
const { PAGE_SIZES } = constants;

// No trailing unit word (user report): the label sits beside #unit-select, which already
// says "cm"/"in" once for the whole row.
test('pageFormatLabel renders the requested unit, ≤2 decimals, zeros trimmed', () => {
  assert.equal(pageFormatLabel('A4'), 'A4 (21 × 29.7)');            // default unit is cm
  assert.equal(pageFormatLabel('A4', 'cm'), 'A4 (21 × 29.7)');
  assert.equal(pageFormatLabel('A4', 'in'), 'A4 (8.27 × 11.69)');
  assert.equal(pageFormatLabel('B5', 'in'), 'B5 (6.93 × 9.84)');
  assert.equal(pageFormatLabel('B1', 'cm'), 'B1 (70.7 × 100)');     // trailing zeros trimmed
  assert.equal(pageFormatLabel('custom', 'in'), 'custom');           // unknown names echo back
});

// A stub <select> with the same options the static template renders (cm labels).
const stubSelect = (values, unit = 'cm') => ({
  value: values[0],
  options: values.map((v) => ({
    value: v,
    textContent: v === 'custom' ? 'Custom' : pageFormatLabel(v, unit),
  })),
});

// applyUnitToUI only reads presentation state, so a minimal `this` suffices.
const runApplyUnitToUI = (unit) =>
  DrawingApp.prototype.applyUnitToUI.call({
    unit, pageSize: 'A4', customPageWidth: 21, customPageHeight: 29.7,
  });

test('applyUnitToUI relabels the toolbar page-size selector in the active unit', () => {
  const names = Object.keys(PAGE_SIZES);
  const psSel = stubSelect(['custom', ...names]);       // toolbar: Custom first
  elements.clear();
  elements.set('page-size', psSel);

  runApplyUnitToUI('in');
  assert.equal(psSel.options[0].textContent, 'Custom', 'Custom label untouched');
  for (const opt of psSel.options.slice(1))
    assert.equal(opt.textContent, pageFormatLabel(opt.value, 'in'), `toolbar ${opt.value} in inches`);
  assert.equal(psSel.value, 'A4', 'toolbar select re-asserts the model page size');

  // And back to cm — labels follow the active unit both ways.
  runApplyUnitToUI('cm');
  for (const opt of psSel.options.slice(1))
    assert.equal(opt.textContent, pageFormatLabel(opt.value, 'cm'), `toolbar ${opt.value} back in cm`);
});

test('applyUnitToUI tolerates the toolbar select not being in the DOM', () => {
  elements.clear();                                      // no selects at all
  assert.doesNotThrow(() => runApplyUnitToUI('in'));
});
