// lib/actionIcon.js — the toolbar icon, redrawn in the service worker so its border
// follows the chosen accent. No DOM there: the badge is painted with OffscreenCanvas 2D
// paths, so the canvas is stubbed and the DRAWING is asserted as the call sequence it is.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applyAccentActionIcon, watchAccentActionIcon } from '../../../src/lib/control/actionIcon.js';
import { ACCENT_HEX, DEFAULT_HL, ACCENT_STORAGE_KEY } from '../../../src/lib/highlight/color.js';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { installDom } from '../../helpers/domStub.js';

// A 2D context that records what it was told to paint, plus the canvas that owns it.
const recorder = () => {
  const canvases = [];
  class Canvas {
    constructor(w, h) {
      this.width = w; this.height = h;
      this.strokes = [];       // strokeStyle at each stroke(), in order
      this.fills = [];
      this.drawn = [];         // downscale sources
      canvases.push(this);
    }
    getContext() {
      const ctx = {
        canvas: this,
        beginPath: () => {}, roundRect: () => {}, arc: () => {}, moveTo: () => {}, lineTo: () => {},
        clearRect: () => {},
        fill: () => this.fills.push(ctx.fillStyle),
        stroke: () => this.strokes.push(ctx.strokeStyle),
        drawImage: (src) => this.drawn.push(src),
        getImageData: (x, y, w, h) => ({ width: w, height: h }),
      };
      return ctx;
    }
  }
  return { Canvas, canvases };
};

const run = async ({ accent } = {}) => {
  const { Canvas, canvases } = recorder();
  const icons = [];
  const chrome = installChromeStub({ local: accent ? { [ACCENT_STORAGE_KEY]: accent } : {} });
  globalThis.chrome.action = { setIcon: async (arg) => { icons.push(arg); } };
  const restore = installDom({ OffscreenCanvas: Canvas });
  try {
    await applyAccentActionIcon();
  } finally { restore(); chrome.restore(); }
  return { canvases, icons };
};

test('all three manifest sizes are rendered and installed in one setIcon call', async () => {
  const { icons } = await run();
  assert.equal(icons.length, 1);
  assert.deepEqual(Object.keys(icons[0].imageData), ['16', '32', '48']);
  for (const [size, data] of Object.entries(icons[0].imageData)) {
    assert.equal(data.width, Number(size));
    assert.equal(data.height, Number(size));
  }
});

// A 16px badge drawn at 16px has jagged ring and dots; it is painted at 4x and downscaled.
test('each size is painted at 4x and downscaled, so the ring stays clean at 16px', async () => {
  const { canvases } = await run();
  const sizes = canvases.map((c) => c.width);
  assert.deepEqual(sizes, [64, 16, 128, 32, 192, 48], 'a 4x canvas then its target, per size');
  for (const target of canvases.filter((c) => [16, 32, 48].includes(c.width)))
    assert.equal(target.drawn.length, 1, 'the target is only ever the downscale');
});

test('the ring wears the stored accent — the first stroke on the badge', async () => {
  const { canvases } = await run({ accent: 'grass' });
  assert.equal(canvases[0].strokes[0], ACCENT_HEX.grass);
});

test('no stored accent, an unknown one, or unreadable storage all fall back to the default', async () => {
  for (const accent of [undefined, 'not-an-accent']) {
    const { canvases } = await run({ accent });
    assert.equal(canvases[0].strokes[0], DEFAULT_HL);
  }
  const { Canvas, canvases } = recorder();
  const chrome = installChromeStub({ storageThrows: true });
  globalThis.chrome.action = { setIcon: async () => {} };
  const restore = installDom({ OffscreenCanvas: Canvas });
  try { await applyAccentActionIcon(); } finally { restore(); chrome.restore(); }
  assert.equal(canvases[0].strokes[0], DEFAULT_HL);
});

// Best-effort by design: the static manifest PNGs stay in place rather than the worker
// dying on a context it cannot get.
test('a failure leaves the manifest icons alone instead of throwing', async () => {
  const chrome = installChromeStub();
  globalThis.chrome.action = { setIcon: async () => { throw new Error('no such tab'); } };
  const restore = installDom({
    OffscreenCanvas: class { constructor() { throw new Error('no OffscreenCanvas'); } },
  });
  const warned = [];
  const warn = console.warn;
  console.warn = (...a) => warned.push(a);
  try {
    await applyAccentActionIcon();
  } finally { console.warn = warn; restore(); chrome.restore(); }
  assert.equal(warned.length, 1);
  assert.match(String(warned[0][0]), /toolbar icon/);
});

test('the watcher re-tints on an accent change, and on nothing else', async () => {
  const chrome = installChromeStub();
  const painted = [];
  globalThis.chrome.action = { setIcon: async (a) => painted.push(a) };
  const { Canvas } = recorder();
  const restore = installDom({ OffscreenCanvas: Canvas });
  try {
    watchAccentActionIcon();
    const [listener] = chrome.changedListeners;
    assert.ok(listener, 'it listens on storage.onChanged');
    listener({ somethingElse: {} }, 'local');
    listener({ [ACCENT_STORAGE_KEY]: {} }, 'sync');
    await Promise.resolve();
    assert.equal(painted.length, 0, 'another key, or another area, is not our change');
    listener({ [ACCENT_STORAGE_KEY]: { newValue: 'pink' } }, 'local');
    await new Promise((r) => setTimeout(r, 0));
    assert.equal(painted.length, 1);
  } finally { restore(); chrome.restore(); }
});
