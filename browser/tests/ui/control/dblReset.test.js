// Tests for js/ui/control/dblReset.js — a double-click puts a selector or a checkbox back to
// its default through the control's own change path. Fakes stand in for the DOM controls.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { defaultOf, resetControl } from '../../../js/ui/control/dblReset.js';

class FakeControl extends EventTarget {
  constructor(props) { super(); Object.assign(this, { id: '', disabled: false, dataset: {} }, props); }
}
const select = (id, value, values, extra = {}) => new FakeControl({
  id, type: 'select-one', value, options: values.map((v) => ({ value: v, defaultSelected: false })), ...extra,
});
const changes = (el) => {
  const seen = [];
  el.addEventListener('change', () => seen.push(el.type === 'checkbox' ? el.checked : el.value));
  return seen;
};

test('a known control takes its default from the app state, not from the markup', () => {
  assert.equal(defaultOf(select('line-style', 'dashed', ['dashed', 'solid'])), 'solid');
  assert.equal(defaultOf(select('page-size', 'A4', ['custom', 'A4', 'A3'])), 'A3');
  assert.equal(defaultOf(new FakeControl({ id: 'show-points', type: 'checkbox', checked: false })), true);
  assert.equal(defaultOf(new FakeControl({ id: 'allow-formulas', type: 'checkbox', checked: true })), false);
});

test('an unknown one falls back to data-default, then the markup, then the first option', () => {
  assert.equal(defaultOf(select('x', 'b', ['a', 'b'], { dataset: { default: 'b' } })), 'b');
  const marked = select('x', 'a', ['a', 'b']);
  marked.options[1].defaultSelected = true;
  assert.equal(defaultOf(marked), 'b');
  assert.equal(defaultOf(select('x', 'b', ['a', 'b'])), 'a');
  assert.equal(defaultOf(new FakeControl({ type: 'checkbox', checked: true, defaultChecked: false })), false);
});

test('a reset applies through change, once, and only when something moves', () => {
  const style = select('line-style', 'dashed', ['solid', 'dashed']);
  const seen = changes(style);
  assert.equal(resetControl(style), true);
  assert.equal(style.value, 'solid');
  assert.deepEqual(seen, ['solid']);
  assert.equal(resetControl(style), false, 'already the default: nothing fires');
  assert.deepEqual(seen, ['solid']);

  const box = new FakeControl({ id: 'show-lines', type: 'checkbox', checked: false });
  const boxSeen = changes(box);
  assert.equal(resetControl(box), true);
  assert.deepEqual(boxSeen, [true]);
});

test('a disabled control, or a default the list does not offer, is left alone', () => {
  assert.equal(resetControl(select('line-style', 'dashed', ['solid', 'dashed'], { disabled: true })), false);
  assert.equal(resetControl(select('x', 'b', ['b'], { dataset: { default: 'gone' } })), false);
});
