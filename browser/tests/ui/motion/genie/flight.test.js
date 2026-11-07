// The genie flight's canvas (js/ui/motion/genie/flight.js): laid over the page where the flight spans, and, handed
// `follow`, moved with that element frame by frame, so a layout shifting the frame mid-flight (a toolbar re-wrapping
// above it) never leaves the picture over what lies above (user report).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../../helpers/dom.js';

const frames = [];
const ctx = new Proxy({}, { get: (t, k) => (k in t ? t[k] : () => {}), set: (t, k, v) => { t[k] = v; return true; } });
installDom({ createElement: (tag) => createStubElement(tag, tag === 'canvas' ? { getContext: () => ctx } : {}) }, {
  innerWidth: 1000, innerHeight: 800, devicePixelRatio: 1,
  performance: { now: () => 0 },
  requestAnimationFrame: (fn) => frames.push(fn),
  cancelAnimationFrame: () => {},
});
const { playGenie } = await import('../../../../js/ui/motion/genie/flight.js');
const box = (left, top, width, height) => ({ left, top, width, height, right: left + width, bottom: top + height });

test('with `follow`, the canvas translates by the element\'s drift on every frame; without it, never', () => {
  const shot = { width: 100, height: 100 };
  let frameTop = 400;
  const frame = { getBoundingClientRect: () => box(0, frameTop, 700, 300) };
  playGenie(shot, box(10, 410, 680, 280), box(300, 698, 80, 2), { opening: true, ms: 500, follow: frame });
  const canvas = globalThis.document.body.children.at(-1);
  assert.equal(canvas.tagName, 'CANVAS');
  frames.shift()(100);
  assert.equal(canvas.style.transform, '', 'still, while the frame stands');
  frameTop = 465;   // the toolbar above re-wrapped: the frame moved 65px down
  frames.shift()(200);
  assert.equal(canvas.style.transform, 'translate(0px, 65px)', 'the flight follows the frame');
  frameTop = 400;
  frames.shift()(300);
  assert.equal(canvas.style.transform, '', 'and comes back with it');
  frames.length = 0;
  playGenie(shot, box(10, 410, 680, 280), box(300, 698, 80, 2), { opening: true, ms: 500 });
  const plain = globalThis.document.body.children.at(-1);
  frameTop = 500;
  frames.shift()(100);
  assert.equal(plain.style.transform ?? '', '', 'a flight with nothing to follow stays where it was laid');
});
