// stencil.EasterEggs — one call per logo show, each chaining back the facade and each running
// the show its accent would not.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp } from './helpers/stencilApiRig.js';
import { createStubElement } from './helpers/dom.js';
import { SHOW_NAMES } from '../js/ui/logo/stageRules.js';
import { logoStageOpen, closeLogoStage } from '../js/ui/logo/stage.js';

// The rig's body only answers the fullscreen class; a stage needs one that takes a child.
const body = createStubElement('body', { classList: { contains: () => false } });
document.body = body;
document.createElement = (tag) => createStubElement(tag);
globalThis.innerWidth = 1000;
globalThis.innerHeight = 600;
globalThis.requestAnimationFrame = () => 1;
globalThis.cancelAnimationFrame = () => {};
globalThis.performance = { now: () => 0 };

const facade = () => createStencil(makeApp());

test('every show in the table is a call on the namespace, plus close, what and of', () => {
  const eggs = facade().EasterEggs;
  for (const name of SHOW_NAMES) assert.equal(typeof eggs[name], 'function', `${name} is missing`);
  for (const extra of ['close', 'what', 'of']) assert.equal(typeof eggs[extra], 'function', extra);
  assert.equal(Object.keys(eggs).length, SHOW_NAMES.length + 3);
});

test('what() lists the words, and of() runs one by its word, however it is cased', () => {
  const stencil = facade();
  assert.deepEqual(stencil.EasterEggs.what(), SHOW_NAMES, 'the words themselves');
  assert.notEqual(stencil.EasterEggs.what(), stencil.EasterEggs.what(), 'a copy, never the table');
  assert.equal(stencil.EasterEggs.of('  WaTeRsHoW '), stencil, 'case and space do not matter');
  assert.equal(logoStageOpen(), true);
  stencil.EasterEggs.close();
  assert.equal(stencil.EasterEggs.of('nosuch'), stencil, 'an unknown word opens nothing');
  assert.equal(logoStageOpen(), false);
});

test('a call opens the show whatever the accent is, and hands the facade back', () => {
  const stencil = facade();
  assert.equal(stencil.mainTheme, 'violet', 'the accent says neon…');
  assert.equal(stencil.EasterEggs.waterShow(), stencil, '…but the call is its own way in');
  assert.equal(logoStageOpen(), true);
  assert.equal(stencil.EasterEggs.close(), stencil);
  assert.equal(logoStageOpen(), false);
});

test('the namespace is read-only, like every other', () => {
  const stencil = facade();
  assert.throws(() => { stencil.EasterEggs = {}; }, /read-only/);
  assert.throws(() => { stencil.EasterEggs.neonOn = () => {}; }, /read-only/);
  assert.throws(() => { delete stencil.EasterEggs.neonOn; }, /cannot be deleted/);
  closeLogoStage();
});
