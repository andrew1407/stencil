// js/ui/shell/unloadFlush.js: a page hidden or leaving lands the trailing save, the co-edit result
// and the thumbnail at once, and a second signal for the same exit writes nothing more.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createTrailingSave } from '../../../js/core/zoom/pan.js';
import { installUnloadFlush } from '../../../js/ui/shell/unloadFlush.js';

const target = () => {
  const on = {};
  return { on, addEventListener: (t, fn) => { (on[t] ||= []).push(fn); }, fire: (t) => (on[t] || []).forEach((fn) => fn()) };
};

const rig = () => {
  const writes = [];
  const timers = new Set();
  const saveSoon = createTrailingSave(() => writes.push('save'), {
    setTimer: (fn) => { const id = { fn }; timers.add(id); return id; }, clearTimer: (id) => timers.delete(id) });
  let resultPending = false;
  const app = {
    storage: { saveSoon, thumbs: { flush: () => writes.push('thumb') } },
    remoteSync: { flushResult: () => { if (resultPending) writes.push('result'); resultPending = false; } },
  };
  const win = target();
  const doc = { ...target(), visibilityState: 'visible' };
  installUnloadFlush(app, win, doc);
  return { app, win, doc, writes, edit: () => { saveSoon(); resultPending = true; } };
};

test('pagehide lands the pending save and result, once', () => {
  const { win, writes, edit } = rig();
  edit();
  win.fire('pagehide');
  win.fire('pagehide');
  assert.deepEqual(writes.filter((w) => w !== 'thumb'), ['save', 'result']);
});

test('a tab going hidden flushes; becoming visible does not', () => {
  const { doc, writes, edit } = rig();
  edit();
  doc.fire('visibilitychange');
  assert.deepEqual(writes, [], 'still visible');
  doc.visibilityState = 'hidden';
  doc.fire('visibilitychange');
  assert.deepEqual(writes, ['save', 'result', 'thumb']);
});
