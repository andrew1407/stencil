// Sliding's window flight (src/lib/motion/surfaces.js genieIn / genieOut): it plays only in Sliding, holds
// the box while it is photographed, falls back to the CSS flight without a picture (node has no DOM to
// draw), and the close's unmount runs as it lands — or at once when a reopen settles it.
import test from 'node:test';
import assert from 'node:assert';

import { genieIn, genieOut, settleGenie, GENIE_OUT_MS } from '../../../src/lib/motion.js';

const stubBox = () => {
  const classes = new Set();
  const props = new Map();
  return {
    classes, props,
    classList: { add: (...c) => c.forEach((x) => classes.add(x)), remove: (...c) => c.forEach((x) => classes.delete(x)) },
    style: { setProperty: (k, v) => props.set(k, v), removeProperty: (k) => props.delete(k) },
    getBoundingClientRect: () => ({ left: 100, top: 200, width: 300, height: 200 }),
  };
};
const ICON = { left: 240, top: 20, width: 20, height: 20 };
const tick = () => new Promise((r) => setTimeout(r, 0));
const withMode = async (mode, fn) => {
  const prev = globalThis.StencilMotion;
  globalThis.StencilMotion = { get: () => mode, reduced: () => mode === 'none', particles: () => false, style: () => null };
  try { await fn(); } finally { if (prev === undefined) delete globalThis.StencilMotion; else globalThis.StencilMotion = prev; }
};

test('outside Sliding nothing flies and the close is the caller\'s to run', () => withMode('particles', () => {
  const box = stubBox();
  let ran = false;
  assert.strictEqual(genieIn(box, ICON), false);
  assert.strictEqual(genieOut(box, ICON, () => { ran = true; }), false);
  assert.strictEqual(ran, false);
  assert.strictEqual(box.classes.size, 0);
}));

test('in Sliding the open stamps the vars and flies; a settle clears them', () => withMode('slide', async () => {
  const box = stubBox();
  assert.strictEqual(genieIn(box, ICON), true);
  assert.ok(box.classes.has('genie-driven'), 'hidden while it is photographed');
  assert.strictEqual(box.props.get('--modal-dy'), '-270px');
  await tick();
  assert.ok(box.classes.has('genie-in') && !box.classes.has('genie-driven'), 'no picture: the CSS flight plays');
  settleGenie(box);
  assert.strictEqual(box.classes.size, 0);
  assert.strictEqual(box.props.size, 0);
}));

test('the close unmounts as it lands, and a reopen settles it at once', () => withMode('slide', async () => {
  const box = stubBox();
  const back = stubBox();
  let hidden = 0;
  assert.strictEqual(genieOut(box, ICON, () => { hidden += 1; }, back), true);
  assert.ok(box.classes.has('genie-hold') && back.classes.has('genie-leaving'), 'held, still visible, while photographed');
  await tick();
  assert.ok(box.classes.has('genie-out'));
  await new Promise((r) => setTimeout(r, GENIE_OUT_MS + 30));
  assert.strictEqual(hidden, 1);
  assert.ok(!back.classes.has('genie-leaving'));

  genieOut(box, ICON, () => { hidden += 1; });
  settleGenie(box);
  assert.strictEqual(hidden, 2, 'settling runs the pending unmount now, once');
  settleGenie(box);
  assert.strictEqual(hidden, 2);
}));
