// Tests for js/ui/control/reset/dblReset.js — a double-click puts a selector, a checkbox or a mirrored menu
// row back to its default through the control's own change path. Fakes stand in for the DOM controls.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { defaultOf, resetControl, installDblReset } from '../../../../js/ui/control/reset/dblReset.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

const { doubleTapMs } = constants.POPOVER;

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

test('a mirrored menu row resets through its own toggle, only while its toolbar check is off default', () => {
  const box = { id: 'show-points', type: 'checkbox', checked: true, dataset: {} };
  box.checked = !defaultOf(box);
  let toggles = 0;
  const row = { id: 'ctx-show-points', ownerDocument: { getElementById: () => box }, click: () => { toggles++; box.checked = !box.checked; } };
  assert.equal(resetControl(row), true);
  assert.equal(resetControl(row), false, 'at its default, nothing to do');
  assert.equal(toggles, 1);
});

test('a number or formula field resets to the editor default; the point colour to the toolbar yellow', () => {
  const thick = new FakeControl({ id: 'line-thickness', type: 'number', value: '9' });
  const seen = changes(thick);
  assert.equal(defaultOf(thick), '2');
  assert.equal(resetControl(thick), true);
  assert.deepEqual(seen, ['2']);
  assert.equal(resetControl(thick), false, 'already the default');
  assert.equal(defaultOf(new FakeControl({ id: 'point-size', type: 'number' })), '4');
  const formula = new FakeControl({ id: 'formula-x', type: 'text', value: 'x*2' });
  let inputs = 0;
  formula.addEventListener('input', () => inputs++);
  assert.equal(resetControl(formula), true);
  assert.equal(formula.value, '');
  assert.equal(inputs, 1, 'the formula commits from its input event');
  assert.equal(defaultOf(new FakeControl({ id: 'point-color', type: 'color', value: '#000000' })), '#ffff00');
});

test('every Visuals row has a default its title can put back, the speed and the sort included', () => {
  assert.equal(defaultOf(new FakeControl({ id: 'vs-motion-speed-value', type: 'number', value: '0.8' })), '0.37');
  assert.equal(defaultOf(new FakeControl({ id: 'vs-motion-speed', type: 'range', value: '0.8' })), '0.37');
  assert.equal(defaultOf(new FakeControl({ id: 'vs-thickness', type: 'number', value: '9' })), '2');
  assert.equal(defaultOf(new FakeControl({ id: 'vs-hold-delay', type: 'number', value: '900' })), '500');
  assert.equal(defaultOf(new FakeControl({ id: 'vs-voice-silence', type: 'number', value: '3000' })), '1000');
  assert.equal(defaultOf(new FakeControl({ id: 'vs-multi-window', type: 'checkbox', checked: true })), false);
  assert.equal(defaultOf(select('projects-sort', 'name', ['opened-desc', 'name'])), 'opened-desc');
  const bar = new FakeControl({ id: 'vs-motion-speed', type: 'range', value: '0.9' });
  const seen = changes(bar);
  assert.equal(resetControl(bar), true);
  assert.deepEqual(seen, ['0.37']);
});

// Touch fires no dblclick (never on iOS): two taps on one control inside doubleTapMs are the double.
test('two taps on a selector inside the double-tap window reset it; slow taps and mouse clicks do not', () => {
  const listeners = {};
  installDblReset({ addEventListener: (type, fn) => (listeners[type] ??= []).push(fn) });
  const fire = (e) => listeners.click.forEach((fn) => fn(e));
  const style = select('line-style', 'dashed', ['solid', 'dashed']);
  style.closest = (sel) => (sel === 'select' ? style : null);
  const tap = (pointerType = 'touch') => ({ target: style, detail: 1, pointerType, stopPropagation() {}, preventDefault() {} });
  const had = globalThis.performance.now;
  const hadDom = [globalThis.Element, globalThis.HTMLInputElement];
  globalThis.Element = EventTarget;             // resetTarget asks `instanceof Element`
  globalThis.HTMLInputElement ??= class {};
  let now = 1000;
  globalThis.performance.now = () => now;
  try {
    fire(tap()); now += doubleTapMs + 1; fire(tap());
    assert.equal(style.value, 'dashed', 'two taps apart are two taps');
    fire(tap()); now += 100; fire(tap());
    assert.equal(style.value, 'solid', 'two taps inside the window reset the selector');
    style.value = 'dashed';
    fire(tap('mouse')); now += 100; fire(tap('mouse'));
    assert.equal(style.value, 'dashed', 'a mouse double is the browser\'s dblclick, not ours');
  } finally { globalThis.performance.now = had; [globalThis.Element, globalThis.HTMLInputElement] = hadDom; }
});
