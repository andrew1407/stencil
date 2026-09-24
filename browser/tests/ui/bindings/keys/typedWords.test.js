// The typed trigger (js/ui/bindings/typedWords.js): a show's own name, typed into the bare
// window, opens it — and a field with the caret in it keeps every key it is given.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../../helpers/dom.js';

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

const { wireTypedWords, matchTypedWord, typedLetter } = await import('../../../../js/ui/bindings/keys/typedWords.js');
const { logoStageOpen, closeLogoStage, currentLogoStage } = await import('../../../../js/ui/logo/stage.js');

const type = (text, over = {}) => {
  for (const key of text) doc.dispatch('keydown', { key, target: body, ...over });
};

test('a buffer is matched by its tail, so a typo before the word still lands', () => {
  assert.equal(matchTypedWord('qqneonon'), 'neonOn');
  assert.equal(matchTypedWord('qqqpinkvibe'), 'pinkVibe');
  assert.equal(matchTypedWord('neono'), null);
  assert.equal(matchTypedWord(''), null);
});

test('a key spells its own Latin letter, else the US letter of its physical position', () => {
  assert.equal(typedLetter({ key: 'p', code: 'KeyP' }), 'p');
  assert.equal(typedLetter({ key: 'a', code: 'KeyQ' }), 'a', 'AZERTY: the label on the key wins');
  assert.equal(typedLetter({ key: 'з', code: 'KeyP' }), 'p', 'Cyrillic: the position wins');
  assert.equal(typedLetter({ key: 'Shift', code: 'ShiftLeft' }), '');
  assert.equal(typedLetter({ key: '1', code: 'Digit1' }), '1', 'any other character still joins the buffer');
});

test('a word typed under a Cyrillic layout opens its show', () => {
  wireTypedWords({ accent: 'violet', customAccent: null }, doc);
  const russian = [['т', 'KeyN'], ['у', 'KeyE'], ['щ', 'KeyO'], ['т', 'KeyN'], ['щ', 'KeyO'], ['т', 'KeyN']];
  for (const [key, code] of russian) doc.dispatch('keydown', { key, code, target: body });
  assert.equal(logoStageOpen(), true);
  closeLogoStage();
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

  // A spin box hands its keys to a line edit inside it: the caret's element is what counts.
  doc.activeElement = createStubElement('input');
  type('neonon');
  doc.activeElement = null;
  assert.equal(logoStageOpen(), false, 'the caret is in a field the keys did not land on');

  // A named key is not a character, so it neither matches nor breaks the run.
  type('neon');
  doc.dispatch('keydown', { key: 'Shift', target: body });
  type('on');
  assert.equal(logoStageOpen(), true, 'Shift between the letters does not break the word');
  closeLogoStage();
});

// The listener captures ahead of a stage's key swallow, so a word typed during a show takes over.
test('another show\'s word typed during a show switches to it; its own word changes nothing', () => {
  wireTypedWords({ accent: 'violet', customAccent: null }, doc);
  type('neonon');
  const neon = currentLogoStage();
  type('neonon');
  assert.equal(currentLogoStage(), neon, 'the show already up is not restarted');
  type('watershow');
  assert.equal(currentLogoStage()?.name, 'waterShow');
  assert.equal(toasts.length, 2);
  closeLogoStage();
});
