// projectThumbZoom: holding Alt over the last rows re-places the doubled preview in the window
// at once, with no mouse move needed.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const listeners = {};
const on = (map) => (type, fn) => { (map[type] ||= []).push(fn); };
const fire = (map, type, e) => { for (const fn of map[type] || []) fn(e); };
const styleOf = () => ({});
const makeEl = (tag) => {
  const el = {
    tag, style: styleOf(), children: [], ev: {}, className: '',
    classList: { toggle() {}, add() {}, remove() {}, contains: () => false },
    addEventListener(t, fn) { on(el.ev)(t, fn); },
    appendChild(c) { el.children.push(c); return c; },
    set innerHTML(_) { el.img = makeEl('img'); },
    querySelector() { return el.img; },
    getBoundingClientRect() {
      const w = parseFloat(el.img?.style.width) || 0;
      const h = parseFloat(el.img?.style.height) || 0;
      return { left: 0, top: 0, width: w + 12, height: h + 12, right: w + 12, bottom: h + 12 };
    },
  };
  return el;
};
globalThis.window = {
  innerWidth: 1200, innerHeight: 800,
  addEventListener: on(listeners), removeEventListener() {}, matchMedia: () => ({ matches: true }),
};
globalThis.document = {
  body: makeEl('body'), documentElement: makeEl('html'),
  createElement: makeEl, addEventListener() {}, removeEventListener() {},
};
globalThis.matchMedia = () => ({ matches: true });   // reduced motion: no dust

const { createThumbZoom } = await import('../../../../js/ui/projects/window/projectThumbZoom.js');

test('Alt keeps the resized preview inside all four window edges without a mouse move', () => {
  const { enableThumbZoom } = createThumbZoom();
  const thumb = makeEl('div');
  thumb.img = Object.assign(makeEl('img'), { src: 'blob:x', naturalWidth: 2000, naturalHeight: 1500 });
  thumb.getBoundingClientRect = () => ({ left: 20, top: 760, right: 60, bottom: 790, width: 40, height: 30 });
  enableThumbZoom(thumb);
  const zoom = () => document.body.children[0];
  const inside = () => {
    const r = zoom().getBoundingClientRect();
    const left = parseFloat(zoom().style.left);
    const top = parseFloat(zoom().style.top);
    assert.ok(left >= 8 && top >= 8, `top-left ${left},${top}`);
    assert.ok(left + r.width <= 1200 - 8 && top + r.height <= 800 - 8, `bottom-right ${left + r.width},${top + r.height}`);
  };
  for (const [x, y] of [[40, 780], [1180, 780], [1180, 20], [40, 20]]) {
    fire(thumb.ev, 'mouseenter', { clientX: x, clientY: y, altKey: false });
    inside();
    const smallTop = parseFloat(zoom().style.top);
    fire(listeners, 'keydown', { key: 'Alt' });
    inside();
    if (y > 400) assert.ok(parseFloat(zoom().style.top) < smallTop, 'the doubled box moved up');
    fire(listeners, 'keyup', { key: 'Alt' });
    inside();
    fire(thumb.ev, 'mouseleave', {});
  }
});
