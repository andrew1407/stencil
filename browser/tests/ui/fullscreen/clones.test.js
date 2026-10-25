// The fullscreen points mirror follows both coordinate tabs (js/ui/fullscreen/clones.js): a row
// change in the points table or in the Lines tab re-clones it once a frame, and only in fullscreen;
// each entry's controls strip leaves the live controls with exactly one mirror listener each.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const observed = [];
globalThis.MutationObserver = class {
  constructor(cb) { this.cb = cb; }
  observe(target, opts) { observed.push({ target, opts, fire: () => this.cb([]) }); }
};
let frames = [];
globalThis.requestAnimationFrame = (fn) => { frames.push(fn); return frames.length; };
const flush = () => { const due = frames; frames = []; due.forEach((fn) => fn()); };
const { followCoordBody, populateFsControls } = await import('../../../js/ui/fullscreen/clones.js');

test('a row change under the panel body re-clones the mirror once a frame, in fullscreen only', () => {
  const body = { id: 'coord-body' };
  let on = true;
  let refreshed = 0;
  followCoordBody(body, () => on, () => { refreshed++; });
  const { target, opts, fire } = observed.at(-1);
  assert.equal(target, body);
  assert.deepEqual(opts, { childList: true, subtree: true, characterData: true });
  fire();
  fire();
  flush();
  assert.equal(refreshed, 1, 'a burst of row writes is one re-clone');
  on = false;
  fire();
  flush();
  assert.equal(refreshed, 1, 'nothing re-clones outside fullscreen');
});

test('no panel body, nothing to follow', () => {
  const before = observed.length;
  followCoordBody(null, () => true, () => {});
  assert.equal(observed.length, before);
});

// A control with live listener bookkeeping; a clone is a fresh element with the same id.
const control = (id, tagName, type) => {
  const on = {};
  return {
    id, tagName, type, on,
    addEventListener: (t, f) => { (on[t] ||= new Set()).add(f); },
    removeEventListener: (t, f) => { on[t]?.delete(f); },
  };
};
const controlsRoot = (els) => ({
  els,
  querySelectorAll: () => els,
  querySelector: (sel) => els.find((e) => `#${e.id}` === sel) ?? null,
  cloneNode() { return controlsRoot(els.map((e) => control(e.id, e.tagName, e.type))); },
  remove() { this.removed = true; },
});

test('every fullscreen entry re-binds the strip: the live controls keep one mirror listener each', () => {
  const live = controlsRoot([control('show-lines', 'INPUT', 'checkbox'), control('page-size', 'SELECT', '')]);
  let strip = null;
  const panel = {
    querySelector: (sel) => (sel === '.controls' ? strip : strip?.querySelector(sel) ?? null),
    appendChild: (c) => { strip = c; },
  };
  const panelLookup = { querySelector: (sel) => (sel === '#controls-body .controls' ? live : null) };
  const saved = globalThis.document;
  globalThis.document = panelLookup;
  try {
    for (let i = 0; i < 4; i++) populateFsControls(panel);
  } finally { globalThis.document = saved; }
  for (const el of live.els) assert.equal(el.on.change.size, 1, `${el.id}: one mirror, not one per entry`);
});
