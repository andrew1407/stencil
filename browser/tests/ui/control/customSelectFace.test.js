// js/ui/control/customSelectFace.js: a select built inside a window that is still hidden measures
// nothing, so it floors at its widest option on its first layout — not on the first open of its
// list, where the box jumped from its value's width to the list's (user report).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const observers = [];
globalThis.ResizeObserver = class {
  constructor(cb) { this.cb = cb; this.targets = []; this.live = true; observers.push(this); }
  observe(el) { this.targets.push(el); }
  disconnect() { this.live = false; }
};
const element = (tag) => {
  const memo = new Map();
  const node = createStubElement(tag, {
    querySelector: (sel) => memo.get(sel) ?? memo.set(sel, createStubElement('span')).get(sel),
  });
  return node;
};
installDom({ createElement: element });
const { buildSelectFace } = await import('../../../js/ui/control/customSelectFace.js');

const select = (labels) => {
  const parent = createStubElement('div', { insertBefore() {} });
  const el = createStubElement('select', { options: labels.map((t) => ({ textContent: t })) });
  el.parentNode = parent;
  return el;
};

test('a hidden select fits its widest option on its first layout, then stops watching', () => {
  const face = buildSelectFace(select(['All', 'Not open elsewhere', 'Local']));
  let laid = false;
  face.cur.getBoundingClientRect = () => ({ width: laid ? face.cur.innerHTML.length * 8 : 0 });
  face.fitToWidestOption();
  assert.equal(face.cur.style.minWidth ?? '', '', 'nothing to measure while the window is hidden');
  assert.equal(observers.length, 1);
  assert.deepEqual(observers[0].targets, [face.wrap], 'it waits on its own box');
  face.fitToWidestOption();
  assert.equal(observers.length, 1, 'one watch, however often it is synced while hidden');
  laid = true;
  observers[0].cb();
  assert.equal(face.cur.style.minWidth, `${'Not open elsewhere'.length * 8}px`, 'the widest option, before any open');
  assert.equal(observers[0].live, false, 'and the watch is dropped once it fits');
});

test('a select already laid out fits at once and never watches', () => {
  observers.length = 0;
  const face = buildSelectFace(select(['Name', 'Manual order']));
  face.cur.getBoundingClientRect = () => ({ width: face.cur.innerHTML.length * 8 });
  face.fitToWidestOption();
  assert.equal(face.cur.style.minWidth, `${'Manual order'.length * 8}px`);
  assert.equal(observers.length, 0);
});
