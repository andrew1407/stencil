// The fullscreen points mirror follows both coordinate tabs (js/ui/fullscreen/clones.js): a row
// change in the points table or in the Lines tab re-clones it once a frame, and only in fullscreen.
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
const { followCoordBody } = await import('../../../js/ui/fullscreen/clones.js');

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
