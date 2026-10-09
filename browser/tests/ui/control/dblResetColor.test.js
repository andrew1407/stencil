// Tests for js/ui/control/dblReset.js's colour fields: a double-click puts one with a stated default
// back through its own change path, and its native picker waits out the double-click window.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

globalThis.Element ??= class extends EventTarget {};
globalThis.HTMLInputElement ??= class extends globalThis.Element {};
const { defaultOf, resetControl, resetTarget, installDblReset } = await import('../../../js/ui/control/dblReset.js');

const { doubleClickMs, doubleTapMs } = constants.POPOVER;
const toolbar = { value: '#123456' };
const doc = { getElementById: (id) => (id === 'line-color' ? toolbar : null) };

class ColorField extends globalThis.HTMLInputElement {
  constructor(props) {
    super();
    Object.assign(this, { type: 'color', disabled: false, dataset: {}, ownerDocument: doc, defaultValue: '#000000', picks: 0 }, props);
  }
  closest() { return null; }
  showPicker() { this.picks++; }
}
const events = (el) => {
  const seen = [];
  for (const t of ['input', 'change']) el.addEventListener(t, () => seen.push(`${t}:${el.value}`));
  return seen;
};
const click = (target, detail, pointerType = 'mouse') => {
  const e = { target, detail, pointerType, prevented: false, preventDefault() { e.prevented = true; } };
  return e;
};
const rig = () => {
  const listeners = {};
  installDblReset({ addEventListener: (type, fn) => (listeners[type] ??= []).push(fn) });
  return (type, e) => listeners[type].forEach((fn) => fn(e));
};

test('the toolbar colour resets to the default line colour; a line\'s own, to the toolbar\'s', () => {
  assert.equal(defaultOf(new ColorField({ id: 'line-color', value: '#ff0000' })), '#ffff00');
  assert.equal(defaultOf(new ColorField({ id: 'sel-color', value: '#ff0000' })), '#123456');
  assert.equal(defaultOf(new ColorField({ id: 'fs-sel-color', value: '#ff0000' })), '#123456');
  assert.equal(defaultOf(new ColorField({ id: 'x', dataset: { default: '#ABC' } })), '#aabbcc');
});

test('every colour control resets: points to the toolbar\'s, a fill to the new-area default, the rest to the factory', async () => {
  const { createEditorState } = await import('../../../js/core/editorState.js');
  const editor = createEditorState();
  const fields = { 'line-color': { value: '#00ff00' }, 'point-color': { value: '#8000ff' }, 'vs-fill': { value: '#abcdef' } };
  const page = { getElementById: (id) => fields[id] ?? null };
  const at = (id) => defaultOf(new ColorField({ id, value: '#010101', ownerDocument: page }));
  // A line's points go back to the toolbar's point colour; the toolbar's own, to following the line.
  assert.deepEqual([at('sel-point-color'), at('fs-sel-point-color'), at('point-color')], ['#8000ff', '#8000ff', '#00ff00']);
  assert.deepEqual([at('sel-fill'), at('fs-sel-fill')], ['#abcdef', '#abcdef']);
  assert.deepEqual([at('filter-color'), at('ctx-tint-color')], [editor.filterColor, editor.filterColor]);
  assert.deepEqual(['vs-line-color', 'vs-fill', 'vs-sel-glow', 'vs-hover-ring', 'vs-focus-ring'].map(at),
    [editor.color, editor.defaultFillColor, editor.selGlowColor, editor.hoverRingColor, editor.focusRingColor]
      .map((c) => c.toLowerCase()));
  assert.equal(at('blank-image-color'), '#ffffff');
  // No bar on the page: the factory values stand in.
  assert.equal(defaultOf(new ColorField({ id: 'sel-point-color', ownerDocument: { getElementById: () => null } })),
    editor.color.toLowerCase());
});

test('a colour reset fires input then change, once, and only when the colour moves', () => {
  const el = new ColorField({ id: 'line-color', value: '#ff0000' });
  const seen = events(el);
  assert.equal(resetControl(el), true);
  assert.deepEqual(seen, ['input:#ffff00', 'change:#ffff00']);
  el.value = '#FFFF00';
  assert.equal(resetControl(el), false, 'the same colour in another case is no move');
  assert.equal(resetControl(new ColorField({ id: 'line-color', value: '#ff0000', disabled: true })), false);
});

test('only a colour field with a stated default is a reset target', () => {
  const stated = new ColorField({ id: 'sel-color' });
  assert.equal(resetTarget(stated), stated);
  assert.equal(resetTarget(new ColorField({ id: 'blank-color-input' })), null);
});

test('a click waits out the double-click window before opening the picker', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const fire = rig();
  const el = new ColorField({ id: 'line-color', value: '#ff0000' });
  const e = click(el, 1);
  fire('click', e);
  assert.equal(e.prevented, true, 'the native picker does not open on the press');
  t.mock.timers.tick(doubleClickMs - 1);
  assert.equal(el.picks, 0);
  t.mock.timers.tick(1);
  assert.equal(el.picks, 1);
  assert.equal(el.value, '#ff0000', 'a single click never resets');
});

test('a second click inside the window resets instead of opening; a tap waits the tap window', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const fire = rig();
  const el = new ColorField({ id: 'sel-color', value: '#ff0000' });
  const seen = events(el);
  fire('click', click(el, 1, 'touch'));
  t.mock.timers.tick(doubleClickMs);
  assert.equal(el.picks, 0, 'a tap waits longer than a click');
  fire('click', click(el, 1, 'touch'));
  t.mock.timers.tick(doubleTapMs);
  assert.equal(el.picks, 0);
  assert.deepEqual(seen, ['input:#123456', 'change:#123456']);
});

test('a keyboard press, or a field without a default, opens natively', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const fire = rig();
  const keyed = click(new ColorField({ id: 'line-color' }), 0);
  fire('click', keyed);
  const plain = click(new ColorField({ id: 'blank-color-input' }), 1);
  fire('click', plain);
  t.mock.timers.tick(doubleTapMs);
  assert.deepEqual([keyed.prevented, plain.prevented, keyed.target.picks, plain.target.picks], [false, false, 0, 0]);
});

test('a double-click on the document resets the field under it and stops there', () => {
  const fire = rig();
  const el = new ColorField({ id: 'sel-color', value: '#ff0000' });
  let stopped = false;
  fire('dblclick', { target: el, stopPropagation: () => { stopped = true; } });
  assert.deepEqual([el.value, stopped], ['#123456', true]);
  stopped = false;
  fire('dblclick', { target: el, stopPropagation: () => { stopped = true; } });
  assert.equal(stopped, false, 'already at its default: the double-click passes on');
});

test('a drag that starts inside the window keeps the picker shut; one before the click does not', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { createIconDrag } = await import('../../../js/ui/drag/iconDrag.js');
  const dragAway = () => {
    const m = createIconDrag({ originRect: () => ({ left: 0, top: 0, right: 10, bottom: 10 }) });
    m.press(5, 5);
    m.move(200, 200);
    m.release(200, 200);
  };
  const fire = rig();
  const el = new ColorField({ id: 'line-color', value: '#ff0000' });
  fire('click', click(el, 1));
  dragAway();
  t.mock.timers.tick(doubleClickMs);
  assert.equal(el.picks, 0, 'the second press dragged the swatch away: no picker over the drop');
  fire('click', click(el, 1));
  t.mock.timers.tick(doubleClickMs);
  assert.equal(el.picks, 1, 'a later click still opens it');
});
