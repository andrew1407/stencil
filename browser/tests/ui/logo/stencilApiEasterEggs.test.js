// stencil.EasterEggs — one call per logo show, each chaining back the facade and each running
// the show its accent would not.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp } from '../../helpers/stencilApiRig.js';
import { createStubElement } from '../../helpers/dom.js';
import { SHOW_NAMES } from '../../../js/ui/logo/stageRules.js';
import { logoStageOpen, closeLogoStage } from '../../../js/ui/logo/stage.js';

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

// Typed at a console, so casing is forgiven: the facade's guard folds a miss to its one
// case-insensitive key. Exact spelling still wins, and the namespace itself folds too.
test('a show answers in any casing, on the namespace and on the facade', () => {
  const st = facade();
  const eggs = st.EasterEggs;
  const name = SHOW_NAMES[0];
  assert.equal(typeof eggs[name.toLowerCase()], 'function', 'all-lower reaches the show');
  assert.equal(typeof eggs[name.toUpperCase()], 'function', 'all-upper reaches it too');
  assert.equal(eggs[name.toLowerCase()], eggs[name], 'the fold resolves to the same function');
  assert.equal(typeof st.eastereggs, 'object', 'the namespace name folds as well');
  assert.equal(typeof eggs.what, 'function');
  assert.equal(typeof eggs.WHAT, 'function');
});

test('an unknown name is still undefined, and writing through a folded name still throws', () => {
  const eggs = facade().EasterEggs;
  assert.equal(eggs.nosuchshow, undefined, 'a fold invents nothing');
  assert.equal(eggs[Symbol.iterator], undefined, 'symbols are untouched');
  assert.throws(() => { eggs[SHOW_NAMES[0].toLowerCase()] = 1; }, /read-only/);
});
