// The typed trigger (js/ui/bindings/typedWords.js): a show's own name, typed into the bare
// window, opens it — and a field with the caret in it keeps every key it is given.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from './helpers/dom.js';

let doc, body, toasts;
beforeEach(() => {
  toasts = [];
  body = createStubElement('body');
  const balloon = createStubElement('stencil-notifications', { notify: (...a) => toasts.push(a) });
  doc = installDom({ body, getElementById: (id) => (id === 'notify-balloon' ? balloon : null) });
  globalThis.window = globalThis.window || {};
  globalThis.innerWidth = 1000; globalThis.innerHeight = 600;
  globalThis.requestAnimationFrame = () => 1;
  globalThis.cancelAnimationFrame = () => {};
  globalThis.performance = { now: () => 0 };
});

const { wireTypedWords, matchTypedWord } = await import('../js/ui/bindings/typedWords.js');
const { logoStageOpen, closeLogoStage } = await import('../js/ui/logo/logoStage.js');

const type = (text, over = {}) => {
  for (const key of text) doc.dispatch('keydown', { key, target: body, ...over });
};

test('a buffer is matched by its tail, so a typo before the word still lands', () => {
  assert.equal(matchTypedWord('qqneonon'), 'neonOn');
  assert.equal(matchTypedWord('qqqpinkvibe'), 'pinkVibe');
  assert.equal(matchTypedWord('neono'), null);
  assert.equal(matchTypedWord(''), null);
});

test('typing a show\'s name opens it, whatever the accent says', () => {
  wireTypedWords({ accent: 'brown', customAccent: null }, doc);
  type('neonon');
  assert.equal(logoStageOpen(), true);
  assert.equal(toasts.length, 1);
  closeLogoStage();
});

test('the buffer clears after a match, so the same word must be typed again', () => {
  wireTypedWords({ accent: 'violet', customAccent: null }, doc);
  type('neonon');
  closeLogoStage();
  type('on', { });                         // the tail of the word alone: no match
  assert.equal(logoStageOpen(), false);
  type('neonon');
  assert.equal(logoStageOpen(), true);
  closeLogoStage();
});

test('a field with the caret in it keeps its keys, and so does any chord', () => {
  wireTypedWords({ accent: 'violet', customAccent: null }, doc);
  const field = createStubElement('input');
  type('neonon', { target: field });
  assert.equal(logoStageOpen(), false, 'typed into a field');

  type('neonon', { ctrlKey: true });
  assert.equal(logoStageOpen(), false, 'held with a modifier');

  // A named key is not a character, so it neither matches nor breaks the run.
  type('neon');
  doc.dispatch('keydown', { key: 'Shift', target: body });
  type('on');
  assert.equal(logoStageOpen(), true, 'Shift between the letters does not break the word');
  closeLogoStage();
});
