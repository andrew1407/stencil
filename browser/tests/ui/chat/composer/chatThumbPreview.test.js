// chatThumbPreview: the Alt-doubled preview is measured at its full size, not squeezed by the
// spot it sat at, so it keeps its margin on all four sides with no mouse move.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const VW = 400;
const VH = 600;
const win = {};
const on = (map) => (t, fn) => { (map[t] ||= []).push(fn); };
// A fixed auto-width box shrinks to the room right of its `left`, as a browser lays it out.
const makeEl = (tag) => {
  const el = {
    tag, style: {}, children: [], ev: {}, complete: true, classes: new Set(),
    addEventListener(t, fn) { on(el.ev)(t, fn); },
    appendChild(c) { el.children.push(c); return c; },
    remove() { el.removed = true; },
    classList: {
      add: (c) => el.classes.add(c),
      toggle: (c, v) => (v ? el.classes.add(c) : el.classes.delete(c)),
    },
    getBoundingClientRect() {
      if (el.rect) return el.rect;
      const full = el.classes.has('chat-thumb-preview-xl') ? 295 : 234;
      const height = el.classes.has('chat-thumb-preview-xl') ? 473 : 269;
      const left = parseFloat(el.style.left) || 0;
      const top = parseFloat(el.style.top) || 0;
      const width = Math.min(full, VW - left);
      return { left, top, width, height, right: left + width, bottom: top + height };
    },
  };
  return el;
};
globalThis.innerWidth = VW;
globalThis.innerHeight = VH;
globalThis.window ??= globalThis;
globalThis.addEventListener = on(win);
globalThis.document = { body: makeEl('body'), createElement: makeEl };

const { wireThumbPreview } = await import('../../../../js/ui/chat/composer/chatThumbPreview.js');
const fire = (map, t, e = {}) => { for (const fn of map[t] || []) fn(e); };

test('the Alt-doubled preview keeps an 8px margin at every corner', () => {
  for (const [x, y] of [[356, 556], [4, 556], [356, 4], [4, 4], [180, 280]]) {
    const thumb = makeEl('img');
    thumb.rect = { left: x, top: y, right: x + 40, bottom: y + 40, width: 40, height: 40 };
    wireThumbPreview(thumb, 'a-long-caption.png');
    fire(thumb.ev, 'mouseenter', { altKey: false });
    const box = document.body.children.at(-1);
    for (const alt of [true, false]) {
      fire(win, alt ? 'keydown' : 'keyup', { key: 'Alt' });
      const r = box.getBoundingClientRect();
      assert.equal(r.width, alt ? 295 : 234, `full width at ${x},${y} alt=${alt}`);
      assert.ok(r.left >= 8 && r.top >= 8, `top-left ${r.left},${r.top}`);
      assert.ok(r.right <= VW - 8 && r.bottom <= VH - 8, `bottom-right ${r.right},${r.bottom}`);
    }
    fire(thumb.ev, 'mouseleave');
  }
});
