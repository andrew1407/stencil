// The options page's logo shows (src/options/secrets/): the browser's words, holds and
// stencil.EasterEggs, with the pink show left to the editor, and the notice's pinned art.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { SHOW_NAMES, effectOf } from '../../../src/lib/logo/stageRules.js';
import { PAGE_SHOWS, OFF_TOAST, heldShow, activateShow, showActive } from '../../../src/options/secrets/trigger.js';
import { matchTypedWord, typedLetter, isTypingTarget } from '../../../src/options/secrets/typedWords.js';
import { SECRET_EGG } from '../../../src/options/secrets/toast.js';
import { createEasterEggs } from '../../../src/options/secrets/easterEggs.js';

const json = (rel) => JSON.parse(readFileSync(new URL(rel, import.meta.url), 'utf8'));
const BROWSER = '../../../../browser/js/config/';

test('the notice\'s egg and the webcore off line are the browser\'s own', () => {
  assert.equal(SECRET_EGG, json(`${BROWSER}svgArt.json`).secretEgg);
  assert.equal(OFF_TOAST, json(`${BROWSER}webcore.json`).strings.off);
});

test('every browser show runs here but pink, which paints an image', () => {
  assert.deepEqual(PAGE_SHOWS, SHOW_NAMES.filter((n) => n !== 'pinkVibe'));
  assert.equal(effectOf('pinkVibe'), 'pink');
  assert.equal(activateShow('pinkVibe'), false);
  assert.equal(showActive('pinkVibe'), false);
});

test('a typed word is the show\'s name lower-cased, under any layout', () => {
  assert.equal(matchTypedWord('xxneonon'), 'neonOn');
  assert.equal(matchTypedWord('webcore'), 'webcore');
  assert.equal(matchTypedWord('neono'), null);
  assert.equal(typedLetter({ key: 'т', code: 'KeyN' }), 'n');
  assert.equal(isTypingTarget({ tagName: 'INPUT', type: 'url' }), true);
  assert.equal(isTypingTarget({ tagName: 'INPUT', type: 'checkbox' }), false);
});

// The page's accent and motion as the pre-paint scripts publish them.
const withPage = (accent, motion, custom, fn) => {
  const saved = { d: globalThis.document, a: globalThis.StencilAccent, m: globalThis.StencilMotion, s: globalThis.StencilSkin };
  const attrs = new Map([['data-accent', accent]]);
  const skin = { on: false, get() { return this.on; }, set(v) { this.on = !!v; attrs.set('data-skin', v ? 'webcore' : null); return this.on; } };
  globalThis.document = {
    documentElement: { getAttribute: (k) => attrs.get(k) ?? null, style: { getPropertyValue: () => custom } },
    getElementById: () => null, querySelector: () => null, body: null,
  };
  globalThis.StencilAccent = { get: () => accent };
  globalThis.StencilMotion = { get: () => motion };
  globalThis.StencilSkin = skin;
  try { return fn(skin); } finally {
    globalThis.document = saved.d; globalThis.StencilAccent = saved.a;
    globalThis.StencilMotion = saved.m; globalThis.StencilSkin = saved.s;
  }
};

// [accent, motion, custom hex, show]: config/logoStage.json as the browser resolves it.
const HELD_CASES = Object.freeze([
  ['violet', 'none', '', 'neonOn'],
  ['grey', 'particles', '', 'dustySpot'],
  ['grey', 'none', '', 'webcore'],
  ['grey', 'fire', '', null],
  ['brown', 'none', '', 'punchToBloat'],
  ['violet', 'none', '#ffffff', 'chaseMe'],
  ['violet', 'none', '#123456', 'randomWalk'],
]);

test('the hold opens the show the accent and motion mode name, as in the browser', () => {
  for (const [accent, motion, custom, show] of HELD_CASES)
    assert.equal(withPage(accent, motion, custom, () => heldShow()), show, `${accent} + ${motion} + '${custom}'`);
});

test('stencil.EasterEggs: the page\'s shows, a word lookup, and webcoreMode as a switch', () => {
  withPage('violet', 'particles', '', (skin) => {
    const eggs = createEasterEggs(globalThis.document);
    assert.deepEqual(eggs.what(), PAGE_SHOWS);
    assert.equal(typeof eggs.neonOn, 'function');
    assert.equal('pinkVibe' in eggs, false);
    assert.equal(eggs.webcoreMode, false);
    eggs.webcoreMode = true;
    assert.equal(skin.on, true);
    eggs.webcoreMode = true;
    assert.equal(skin.on, true, 'assigning the state it is in does nothing');
    eggs.webcoreMode = false;
    assert.equal(skin.on, false);
    assert.equal(eggs.of('  NEONON '), eggs, 'a call answers the facade, so calls chain');
  });
});
