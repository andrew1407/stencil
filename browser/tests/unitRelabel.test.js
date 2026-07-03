// Pins the shared page-format option-label contract (SPEC §1.3): pageFormatLabel
// renders in the requested display unit, and DrawingApp.applyUnitToUI re-labels BOTH
// selectors that use it — the toolbar #page-size select AND the links-modal quick-crop
// #links-crop-pagesize select — so neither stays frozen at the boot-time cm labels
// when the user switches units (parity with the desktop LinksDialog, which rebuilds
// its combo in the live unit on every open).

import { test } from 'node:test';
import assert from 'node:assert/strict';

// Minimal DOM stubs so drawingApp.js (and its import graph) loads under node --test.
// Installed BEFORE the dynamic import; getElementById reads from a per-test map.
const elements = new Map();
globalThis.window = globalThis.window ?? {};
globalThis.window.dispatchEvent = globalThis.window.dispatchEvent ?? (() => {});
globalThis.window.addEventListener = globalThis.window.addEventListener ?? (() => {});
globalThis.document = globalThis.document ?? {};
globalThis.document.getElementById = (id) => elements.get(id) ?? null;
globalThis.document.querySelectorAll = globalThis.document.querySelectorAll ?? (() => []);
globalThis.document.createElement = globalThis.document.createElement ?? (() => ({ getContext: () => null }));

const { DrawingApp } = await import('../js/core/drawingApp.js');
const { pageFormatLabel } = await import('../js/core/units.js');
const constants = (await import('../js/config/constants.json', { with: { type: 'json' } })).default;
const { PAGE_SIZES } = constants;

test('pageFormatLabel renders the requested unit, ≤2 decimals, zeros trimmed', () => {
  assert.equal(pageFormatLabel('A4'), 'A4 (21 × 29.7 cm)');            // default unit is cm
  assert.equal(pageFormatLabel('A4', 'cm'), 'A4 (21 × 29.7 cm)');
  assert.equal(pageFormatLabel('A4', 'in'), 'A4 (8.27 × 11.69 in)');
  assert.equal(pageFormatLabel('B5', 'in'), 'B5 (6.93 × 9.84 in)');
  assert.equal(pageFormatLabel('B1', 'cm'), 'B1 (70.7 × 100 cm)');     // trailing zeros trimmed
  assert.equal(pageFormatLabel('custom', 'in'), 'custom');             // unknown names echo back
});

// A fake <select> with the same options the static template renders (cm labels).
const fakeSelect = (values, unit = 'cm') => ({
  value: values[0],
  options: values.map((v) => ({
    value: v,
    textContent: v === 'custom' ? 'Custom…' : pageFormatLabel(v, unit),
  })),
});

// applyUnitToUI only reads presentation state, so a minimal `this` suffices.
const runApplyUnitToUI = (unit) =>
  DrawingApp.prototype.applyUnitToUI.call({
    unit, pageSize: 'A4', customPageWidth: 21, customPageHeight: 29.7,
  });

test('applyUnitToUI relabels the toolbar AND links-modal quick-crop selectors', () => {
  const names = Object.keys(PAGE_SIZES);
  const psSel = fakeSelect(['custom', ...names]);       // toolbar: Custom… first
  const qcSel = fakeSelect(names);                      // quick-crop: named formats only
  elements.clear();
  elements.set('page-size', psSel);
  elements.set('links-crop-pagesize', qcSel);

  runApplyUnitToUI('in');
  assert.equal(psSel.options[0].textContent, 'Custom…', 'Custom… label untouched');
  for (const opt of psSel.options.slice(1))
    assert.equal(opt.textContent, pageFormatLabel(opt.value, 'in'), `toolbar ${opt.value} in inches`);
  for (const opt of qcSel.options)
    assert.equal(opt.textContent, pageFormatLabel(opt.value, 'in'), `quick-crop ${opt.value} in inches`);
  assert.equal(psSel.value, 'A4', 'toolbar select re-asserts the model page size');

  // And back to cm — labels follow the active unit both ways.
  runApplyUnitToUI('cm');
  for (const opt of qcSel.options)
    assert.equal(opt.textContent, pageFormatLabel(opt.value, 'cm'), `quick-crop ${opt.value} back in cm`);
});

test('applyUnitToUI tolerates the links modal (or toolbar) not being in the DOM', () => {
  elements.clear();                                      // no selects at all
  assert.doesNotThrow(() => runApplyUnitToUI('in'));
});
