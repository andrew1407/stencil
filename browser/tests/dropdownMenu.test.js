// Select dropdowns escape their container (js/ui/dropdownMenu.js). `.controls` clips its overflow, so a list
// longer than the panel was sliced off at its edge and nothing kept a list inside the WINDOW either: an open
// menu is moved to <body> and placed in viewport coordinates. These cases pin the placement, the portal and
// the put-back.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { COMPONENTS_CSS } from './helpers/css.js';

// A DOM small enough to place a menu in, big enough to move it around.
const setupDom = ({ vw = 1200, vh = 800 } = {}) => {
  const listeners = [];
  const el = (rect, extra = {}) => {
    const classes = new Set();
    return {
      style: {}, hidden: true, classes,
      classList: {
        add: (c) => classes.add(c),
        remove: (c) => classes.delete(c),
        toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
      },
      getBoundingClientRect: () => rect, parentElement: null, ...extra,
    };
  };
  globalThis.window = {
    innerWidth: vw, innerHeight: vh,
    addEventListener: (t, f, c) => listeners.push([t, f, c]),
    removeEventListener: (t, f, c) => {
      const i = listeners.findIndex(([lt, lf]) => lt === t && lf === f);
      if (i !== -1) listeners.splice(i, 1);
    },
  };
  const body = { children: [], appendChild(n) { this.children.push(n); n.parentElement = body; } };
  globalThis.document = { body };
  return { el, body, listeners };
};

test('a menu drops below its trigger, left edges aligned, at least as wide', async () => {
  const { el } = setupDom();
  const { placeMenu } = await import('../js/ui/control/dropdownMenu.js');
  const trigger = el({ left: 300, top: 100, bottom: 130, width: 190 });
  const menu = el({ width: 190, height: 200 });
  placeMenu(menu, trigger);
  assert.equal(menu.style.left, '300px');
  assert.equal(menu.style.top, '134px', 'trigger bottom + the 4px gap');
  assert.equal(menu.style.minWidth, '190px', 'never narrower than the control it belongs to');
});

test('a trigger low in the window flips its menu above instead of running off', async () => {
  const { el } = setupDom({ vh: 400 });
  const { placeMenu } = await import('../js/ui/control/dropdownMenu.js');
  const trigger = el({ left: 20, top: 330, bottom: 360, width: 120 });
  const menu = el({ width: 120, height: 200 });
  placeMenu(menu, trigger);
  assert.equal(menu.style.top, '126px', 'sits above: trigger top - gap - height');
  // …and it is marked, so the open/close animation grows from its BOTTOM corner — the one
  // nearest the trigger — instead of appearing to come from somewhere it never was.
  assert.ok(menu.classes.has('dd-above'), 'a flipped menu is marked');
  const below = el({ width: 120, height: 40 });
  placeMenu(below, el({ left: 20, top: 10, bottom: 40, width: 120 }));
  assert.ok(!below.classes.has('dd-above'), 'a normal one is not');
});

test('the height cap follows the room on the roomier side, so a long list scrolls', async () => {
  const { el } = setupDom({ vh: 300 });
  const { placeMenu } = await import('../js/ui/control/dropdownMenu.js');
  const menu = el({ width: 120, height: 260 });
  placeMenu(menu, el({ left: 20, top: 200, bottom: 230, width: 120 }));
  assert.equal(menu.style.maxHeight, '188px', 'above-the-trigger room, less gap and margin');
});

test('a menu wider than the space left of the edge is clamped inside the viewport', async () => {
  const { el } = setupDom({ vw: 500 });
  const { placeMenu } = await import('../js/ui/control/dropdownMenu.js');
  const trigger = el({ left: 430, top: 10, bottom: 40, width: 60 });
  const menu = el({ width: 300, height: 100 });
  placeMenu(menu, trigger);
  assert.equal(menu.style.left, '192px', 'viewport width - margin - box width');
});

test('the cap never exceeds the stylesheet\'s 280px, however tall the window', async () => {
  const { el } = setupDom({ vh: 2000 });
  const { placeMenu } = await import('../js/ui/control/dropdownMenu.js');
  const menu = el({ width: 200, height: 100 });
  placeMenu(menu, el({ left: 0, top: 10, bottom: 40, width: 100 }));
  assert.equal(menu.style.maxHeight, '280px');
});

test('open moves the menu to <body> and close puts it back where it was built', async () => {
  const { el, body, listeners } = setupDom();
  const { showMenu, hideMenu } = await import('../js/ui/control/dropdownMenu.js');
  const sibling = {};
  const wrap = { insertBefore(n, before) { this.last = [n, before]; n.parentElement = wrap; } };
  const menu = el({ width: 100, height: 80 }, { parentElement: wrap, nextSibling: sibling });
  sibling.parentElement = wrap;
  const trigger = el({ left: 10, top: 10, bottom: 40, width: 100 });

  showMenu(menu, trigger);
  assert.equal(menu.parentElement, body, 'portaled out of the clipping container');
  assert.equal(menu.hidden, false);
  assert.ok(listeners.some(([t]) => t === 'resize') && listeners.some(([t]) => t === 'scroll'),
    'follows the trigger while it is open');

  hideMenu(menu);
  assert.equal(menu.hidden, true);
  assert.deepEqual(wrap.last, [menu, sibling], 'back in its own markup, in its old slot');
  assert.equal(listeners.length, 0, 'and stops listening');
  for (const p of ['left', 'top', 'minWidth', 'maxHeight']) assert.equal(menu.style[p], '');
});

// The page can move under an open list — the chat panel sliding in re-lays it, a modal re-centres with it —
// and `resize`/`scroll` never fire for that, so the list has to be re-placed on its own.
test('an open menu follows its trigger when the page moves under it', async () => {
  const { el, body, listeners } = setupDom();
  // A hand-cranked rAF: each tick runs whatever the tracker queued.
  const queue = [];
  globalThis.requestAnimationFrame = (fn) => { queue.push(fn); return queue.length; };
  globalThis.cancelAnimationFrame = (id) => { queue[id - 1] = null; };
  const tick = (n = 1) => {
    for (let i = 0; i < n; i++) {
      const pending = queue.splice(0, queue.length);
      for (const fn of pending) fn?.();
    }
  };
  let at = { left: 300, top: 200, bottom: 230, width: 180, height: 30 };
  const trigger = el(null);
  trigger.getBoundingClientRect = () => at;
  const menu = el({ left: 0, top: 0, width: 200, height: 120 });
  // Its own markup home, so hideMenu can put it back (the shell it was built in).
  const home = { insertBefore: (node) => { node.parentElement = home; } };
  menu.parentElement = home;

  const { showMenu, hideMenu } = await import('../js/ui/control/dropdownMenu.js');
  showMenu(menu, trigger);
  assert.equal(menu.style.left, '300px');
  tick(2);
  assert.equal(menu.style.left, '300px', 'a still trigger is not re-placed');

  at = { ...at, left: 40 };            // the page slides under the open list
  tick(2);
  assert.equal(menu.style.left, '40px', 'the list came with it');

  hideMenu(menu);
  const before = queue.length;
  tick(2);
  at = { ...at, left: 500 };
  tick(2);
  assert.equal(menu.style.left, '', 'a closed menu is not tracked (or re-placed) any more');
  assert.ok(before >= 0);
  assert.equal(listeners.length, 0);
  delete globalThis.requestAnimationFrame;
  delete globalThis.cancelAnimationFrame;
});

test('every select dropdown goes through the portal, and its press-outside sees it', () => {
  const cs = readFileSync(new URL('../js/ui/control/customSelect.js', import.meta.url), 'utf8');
  const ap = readFileSync(new URL('../js/ui/accent/accentPicker.js', import.meta.url), 'utf8');
  for (const [name, src, host] of [['customSelect', cs, 'wrap'], ['accentPicker', ap, 'mount']]) {
    assert.match(src, /showMenu\(menu, trigger\)/, `${name} opens through dropdownMenu`);
    assert.match(src, /hideMenu\(menu\)/, `${name} closes through it`);
    // The menu is on <body> while open, so an outside press must test it too.
    assert.match(src, new RegExp(`!${host}\\.contains\\(e\\.target\\) && !menu\\.contains\\(e\\.target\\)`),
      `${name} does not close on a press inside its own portaled menu`);
  }
  const css = COMPONENTS_CSS;
  assert.match(css, /\.accent-dd-menu\.dd-portal \{[^}]*position: fixed/, 'the portaled menu is viewport-positioned');
  assert.match(css, /\.accent-dd-menu\.dd-portal \{[^}]*right: auto/, 'and anchored from the left it was given');
});
