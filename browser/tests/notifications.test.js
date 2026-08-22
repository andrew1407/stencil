// The bottom-left toast stack (js/ui/notifications.js). It used to be ONE balloon that
// each message overwrote; it now stacks, which means it also needs a ceiling — a burst
// (flipping the theme a few times) otherwise walls off the side of the canvas.
// Node has no DOM, so the component runs against the minimum stub one it actually
// touches: createElement, appendChild, children, classList, querySelector, remove.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { StencilNotifications, MAX_VISIBLE } from '../js/ui/notifications.js';

const mkEl = () => {
  const classes = new Set();
  const parts = {};
  const el = {
    parent: null,
    parts,
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
    },
    set className(v) { classes.clear(); String(v).split(/\s+/).filter(Boolean).forEach((c) => classes.add(c)); },
    get className() { return [...classes].join(' '); },
    setAttribute() {},
    addEventListener: (type, fn) => { if (type === 'click') el.click = fn; },
    set innerHTML(_v) { /* the icon/text spans are stubbed by querySelector */ },
    querySelector: (sel) => (parts[sel] ||= { innerHTML: '', textContent: '' }),
    remove: () => {
      const i = el.parent?.children.indexOf(el) ?? -1;
      if (i >= 0) el.parent.children.splice(i, 1);
    },
    get text() { return parts['.notify-text']?.textContent; },
    get leaving() { return classes.has('notify-leaving'); },
  };
  return el;
};

// A stack instance with just enough element behaviour hung off it.
const mkStack = () => {
  const stack = new StencilNotifications();
  stack.children = [];
  stack.appendChild = (child) => { child.parent = stack; stack.children.push(child); };
  return stack;
};

const withDom = (fn) => {
  const prior = globalThis.document;
  globalThis.document = { createElement: () => mkEl() };
  try { return fn(); } finally { globalThis.document = prior; }
};

const standing = (stack) => stack.children.filter((c) => !c.leaving);

test('the stack never shows more than three toasts at once', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom(() => {
    const stack = mkStack();
    for (let i = 1; i <= 6; i++) stack.notify(`Toast ${i}`, 'info');
    // Three standing, and they are the NEWEST three — the oldest are the ones nobody
    // is still reading.
    assert.deepEqual(standing(stack).map((c) => c.text), ['Toast 4', 'Toast 5', 'Toast 6']);
    // The retired ones play their exit and then leave the DOM entirely.
    t.mock.timers.tick(300);
    assert.deepEqual(stack.children.map((c) => c.text), ['Toast 4', 'Toast 5', 'Toast 6']);
  });
});

test('a toast retired early by the cap does not stage its exit twice', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom(() => {
    const stack = mkStack();
    for (let i = 1; i <= 4; i++) stack.notify(`Toast ${i}`, 'ok');
    const retired = stack.children.find((c) => c.text === 'Toast 1');
    assert.ok(retired.leaving, 'the oldest is on its way out');
    let removals = 0;
    const realRemove = retired.remove;
    retired.remove = () => { removals++; realRemove(); };
    // Its own auto-hide timer still fires later; that must be a no-op, not a second exit
    // (which would re-add the class to an element already gone from the DOM and schedule
    // another removal).
    t.mock.timers.tick(10_000);
    assert.equal(removals, 1, 'the early exit removed it once; the stale timer did nothing');
    // A second tick flushes the removal timers those auto-hides scheduled (mock timers
    // don't run a timer set DURING the tick that is already due).
    t.mock.timers.tick(1_000);
    assert.equal(stack.children.length, 0, 'everything eventually clears');
  });
});

test('the newest toast is appended last, so it sits at the bottom of the column', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom(() => {
    const stack = mkStack();
    stack.notify('first', 'ok');
    stack.notify('second', 'fail');
    assert.deepEqual(stack.children.map((c) => c.text), ['first', 'second']);
    assert.ok(stack.children[1].className.includes('notify-fail'), 'type rides on the toast');
  });
});

test('a clickable toast runs its action once and dismisses itself', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom(() => {
    const stack = mkStack();
    let ran = 0;
    stack.notify('Assistant finished', 'info', { onClick: () => { ran++; } });
    const toast = stack.children[0];
    assert.ok(toast.className.includes('notify-clickable'));
    toast.click();
    assert.equal(ran, 1);
    assert.ok(toast.leaving, 'clicking dismisses it');
    t.mock.timers.tick(300);
    assert.equal(stack.children.length, 0);
  });
});

test('the cap matches the desktop stack, and the CSS stacks the column', () => {
  const hpp = readFileSync(new URL('../../desktop/src/support/notifications.hpp', import.meta.url), 'utf8');
  assert.equal(MAX_VISIBLE, Number(/kMaxVisible = (\d+)/.exec(hpp)[1]),
    'browser and desktop must agree on how many toasts are too many');

  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  const host = css.slice(css.indexOf('#notify-balloon {'));
  const hostBlock = host.slice(0, host.indexOf('}'));
  assert.match(hostBlock, /flex-direction: column/, 'the host is the column, not a toast');
  assert.match(hostBlock, /pointer-events: none/, 'the empty column never eats clicks');
  assert.ok(!/background:/.test(hostBlock), 'the balloon look moved to .notify-toast');
  assert.match(css, /\.notify-toast \{/, 'per-message toasts carry it');
  assert.match(css, /\.notify-toast\.notify-clickable \{ pointer-events: auto/,
    'only a clickable toast opts back into pointer events');

  // The entrance must not fill FORWARDS: pinning transform:none would kill the
  // clickable toast's hover lift.
  const anims = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.match(anims, /animation: notifyEnter [^;]*backwards;/, 'entrance fills backwards only');
  assert.match(anims, /\.notify-toast\.notify-leaving \{\n\s*animation: notifyLeave/, 'and the exit is its own');
});

// ── Coalescing: a duplicate message extends, never stacks ───────────────────
// A zoom burst raises a "Saved" per step; the stack must show ONE toast whose
// lifetime keeps extending — same element, so the entrance animation is never
// replayed and nothing flickers.

test('an identical toast coalesces: same element, extended lifetime, no flicker', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom(() => {
    const stack = mkStack();
    stack.notify('Saved', 'ok');
    const first = stack.children[0];
    t.mock.timers.tick(2000);            // 400ms short of the 2400ms auto-hide
    stack.notify('Saved', 'ok');
    stack.notify('Saved', 'ok');
    assert.equal(stack.children.length, 1, 'duplicates never stack');
    assert.equal(stack.children[0], first, 'the SAME element — entrance not replayed, no flicker');
    assert.ok(!first.leaving);
    t.mock.timers.tick(1000);            // past the ORIGINAL expiry (t=3000 > 2400)
    assert.ok(!first.leaving, 'the refreshed lifetime outlives the original timer');
    t.mock.timers.tick(2000);            // past the refreshed expiry (t=5000 > 4400)
    assert.ok(first.leaving, 'then it retires once, normally');
  });
});

test('distinct texts still stack, and the same words in a different kind stack too', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom(() => {
    const stack = mkStack();
    stack.notify('Saved', 'ok');
    stack.notify('Connected', 'ok');
    assert.equal(standing(stack).length, 2, 'different messages are different events');
    stack.notify('Saved', 'fail');
    assert.equal(standing(stack).length, 3, 'same text but another kind is not the same status');
  });
});

test('clickable toasts never coalesce — each carries its own action', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  withDom(() => {
    const stack = mkStack();
    let a = 0;
    let b = 0;
    stack.notify('Assistant finished', 'info', { onClick: () => a++ });
    stack.notify('Assistant finished', 'info', { onClick: () => b++ });
    assert.equal(standing(stack).length, 2, 'two actions, two affordances');
    // …and a plain duplicate must not fold itself into a clickable toast either
    // (refreshing it would stretch an actionable affordance it has nothing to do with).
    stack.notify('Assistant finished', 'info');
    assert.equal(standing(stack).length, 3);
  });
});
