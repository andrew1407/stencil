// Tests for js/ui/numericInput.js — the arithmetic every numeric field accepts.
// The evaluator is pure, so it imports straight into Node. Its operator set matches
// core/parse/formulaParser (+ - * / ** and parens, ** right-associative), minus the
// variable: a numeric field takes a constant expression, not f(x).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  evalNumericExpression as ev, enhanceNumericInput, COMMIT_DEBOUNCE_MS,
} from '../../../js/ui/control/numericInput.js';

test('plain numbers pass straight through', () => {
  assert.equal(ev('54'), 54);
  assert.equal(ev('  54  '), 54);
  assert.equal(ev('29.7'), 29.7);
  assert.equal(ev('.5'), 0.5);
  assert.equal(ev('-5'), -5);          // a leading '-' is a SIGN, not "current minus 5"
  assert.equal(ev('+5'), 5);
});

test('the headline case: an expression is evaluated on commit', () => {
  assert.equal(ev('45 + 9'), 54);
  assert.equal(ev('45+9'), 54);
  assert.equal(ev('100 - 1'), 99);
  assert.equal(ev('7 * 6'), 42);
  assert.equal(ev('84 / 2'), 42);
});

test('a leading * or / continues from the value already in the field', () => {
  assert.equal(ev('* 9', 3), 27);       // the "type 3, then *9" case
  assert.equal(ev('*9', 3), 27);
  assert.equal(ev('/2', 10), 5);
  assert.equal(ev('**2', 5), 25);
  assert.equal(ev('^2', 5), 25);
  // …but + and - stay signs, so a negative number still means what it says.
  assert.equal(ev('-5', 10), -5);
  assert.equal(ev('+5', 10), 5);
});

test('precedence, parentheses and right-associative **', () => {
  assert.equal(ev('2 + 3 * 4'), 14);
  assert.equal(ev('(2 + 3) * 4'), 20);
  assert.equal(ev('2 ** 3 ** 2'), 512);      // right-assoc: 2**(3**2), not (2**3)**2
  assert.equal(ev('-2 ** 2'), -4);           // unary binds looser than **
  assert.equal(ev('2 * (3 + (4 - 1))'), 12);
});

test('invalid input answers null so the caller can keep the old value', () => {
  for (const bad of ['', '   ', 'abc', '45 +', '+', '()', '(1', '1)', '1 2', '45 & 9', '1/0', '$5', null, undefined, 42]) {
    assert.equal(ev(bad), null, `expected null for ${JSON.stringify(bad)}`);
  }
});

test('non-finite results are rejected, not returned as Infinity/NaN', () => {
  assert.equal(ev('1/0'), null);
  assert.equal(ev('10 ** 400'), null);   // overflows to Infinity
});

test('a missing current value treats the field as 0', () => {
  assert.equal(ev('* 9'), 0);
  assert.equal(ev('*9', undefined), 0);
});

// The commit timer must never edit text the user is still writing: a hand-built stub input plus node's
// built-in mock timers drive enhanceNumericInput's whole lifecycle.

globalThis.document ??= { activeElement: null };

const makeInput = (value, attrs = {}) => {
  const listeners = new Map();
  const el = {
    dataset: {}, value: String(value), type: 'number', inputMode: '', autocomplete: '',
    seen: [],                                   // events the field dispatched at us
    getAttribute: (k) => ({ step: '1', ...attrs }[k] ?? null),
    addEventListener(t, fn) {
      if (!listeners.has(t)) listeners.set(t, []);
      listeners.get(t).push(fn);
    },
    dispatchEvent(e) {
      el.seen.push(e.type);
      for (const fn of listeners.get(e.type) || []) fn(e);
      return true;
    },
    blur() { el.fire('blur'); },
    // Simulate a keystroke: the caller sets el.value first, exactly like a browser does.
    fire(type, extra = {}) {
      const e = { type, stopImmediatePropagation() {}, preventDefault() {}, ...extra };
      for (const fn of listeners.get(type) || []) fn(e);
    },
    type_(text) { el.value = text; el.fire('input'); },
  };
  enhanceNumericInput(el);
  return el;
};

test('a pause mid-expression leaves the half-typed text alone', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const el = makeInput('10');
  el.type_('10 *');
  t.mock.timers.tick(COMMIT_DEBOUNCE_MS * 2);          // think for as long as you like
  assert.equal(el.value, '10 *', 'the typed text survived the idle commit');
  assert.deepEqual(el.seen, [], 'nothing was applied to the app');
  // …then finish it, and the completed expression applies as usual.
  el.type_('10 * 3');
  t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
  assert.equal(el.value, '30');
  assert.deepEqual(el.seen, ['input', 'change']);
});

test('clearing the field to retype keeps it clear instead of slamming the old value back', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const el = makeInput('21');
  el.type_('');                                        // select-all + delete
  t.mock.timers.tick(COMMIT_DEBOUNCE_MS * 2);
  assert.equal(el.value, '', 'the field stayed empty for the user to retype');
  assert.deepEqual(el.seen, []);
  el.type_('42');
  t.mock.timers.tick(COMMIT_DEBOUNCE_MS);
  assert.equal(el.value, '42');
});

test('editing ends → an unfinished or empty field falls back to the last good value', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const unfinished = makeInput('21');
  unfinished.type_('21 +');
  unfinished.fire('blur');
  assert.equal(unfinished.value, '21');

  const cleared = makeInput('21');
  cleared.type_('');
  cleared.fire('blur');
  assert.equal(cleared.value, '21');

  const onEnter = makeInput('21');
  onEnter.type_('(3 + ');
  onEnter.fire('keydown', { key: 'Enter' });
  assert.equal(onEnter.value, '21');
});

test('the idle commit waits the full delay, then applies', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const el = makeInput('3');
  el.type_('* 9');                                     // relative edit, off the committed 3
  t.mock.timers.tick(COMMIT_DEBOUNCE_MS - 1);
  assert.equal(el.value, '* 9', 'still typing as far as the field is concerned');
  t.mock.timers.tick(1);
  assert.equal(el.value, '27');
});

test('Escape and blur still commit / restore immediately, no waiting', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const el = makeInput('5');
  el.type_('8');
  el.fire('blur');
  assert.equal(el.value, '8');
  el.type_('99');
  el.fire('keydown', { key: 'Escape' });
  assert.equal(el.value, '8', 'Escape puts the committed value back');
});
