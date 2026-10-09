// js/core/image/decodeLimit.js and the load that holds a picture to it: a picture past the shared
// cap (LIMITS.imageMaxSide / imageMaxPixels) is refused before it is drawn, the picture on screen
// stays, and a canvas the browser will not allocate fails by name instead of a null-context TypeError.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const notes = [];
installDom().register('notify-balloon', createStubElement('div', { notify: (msg, kind) => notes.push([msg, kind]) }));
const { imageTooLarge, context2d, IMAGE_TOO_LARGE, CANVAS_UNAVAILABLE } = await import('../../../js/core/image/decodeLimit.js');
const { loadImageFromFile } = await import('../../../js/core/image/loadFlow.js');

const { imageMaxSide: SIDE, imageMaxPixels: AREA } = constants.LIMITS;

test('the cap: a side past imageMaxSide, or the area past imageMaxPixels', () => {
  assert.equal(imageTooLarge(SIDE, 1), false);
  assert.equal(imageTooLarge(SIDE + 1, 1), true);
  assert.equal(imageTooLarge(1, SIDE + 1), true);
  assert.equal(imageTooLarge(SIDE, Math.floor(AREA / SIDE)), false);
  assert.equal(imageTooLarge(SIDE, Math.floor(AREA / SIDE) + 1), AREA / SIDE < SIDE + 1);
  assert.equal(imageTooLarge(4000, 3000), false);
});

test('context2d names the refused canvas instead of handing back null', () => {
  const ctx = {};
  assert.equal(context2d({ getContext: () => ctx }), ctx);
  assert.throws(() => context2d({ getContext: () => null }), new RegExp(CANVAS_UNAVAILABLE));
});

// The decode boundary: the read answers at once and the picture decodes to the stub's size.
let size = { w: 10, h: 10 };
globalThis.FileReader = class { readAsDataURL() { queueMicrotask(() => this.onload?.({ target: { result: 'data:image/png;base64,AA==' } })); } };
globalThis.Image = class {
  get naturalWidth() { return size.w; }
  get naturalHeight() { return size.h; }
  set src(v) { queueMicrotask(() => this.onload?.()); }
};
const settled = () => new Promise((r) => setTimeout(r, 0));

const replaceApp = () => {
  const old = { width: 4, height: 3 };
  return { old, app: { originalImage: old, imageDataUrl: 'data:old', lines: [], storage: { temporary: false, incognito: false } } };
};

test('a picture past the cap is refused before it is drawn: the original on screen stays', async () => {
  notes.length = 0;
  size = { w: SIDE + 1, h: 2 };
  const { app, old } = replaceApp();
  loadImageFromFile(app, { name: 'bomb.png' }, { replaceInPlace: true });
  await settled();
  assert.equal(app.originalImage, old);
  assert.equal(app.imageDataUrl, 'data:old');
  assert.deepEqual(notes, [[IMAGE_TOO_LARGE, 'fail']]);
});

test('a load whose settle fails (a canvas the browser refuses) is a notice, not an uncaught TypeError', async () => {
  notes.length = 0;
  size = { w: 20, h: 10 };
  const { app } = replaceApp();
  loadImageFromFile(app, { name: 'big.png' }, { replaceInPlace: true });
  await settled();
  assert.equal(app.originalImage.naturalWidth, 20, 'a picture under the cap becomes the original');
  assert.equal(notes.length, 1);
  assert.match(notes[0][0], /^Could not open the image: /);
});
