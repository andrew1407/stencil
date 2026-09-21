// Cross-origin preview media (js/ui/mediaCors.js): the CORS ask, the one plain retry when a
// host refuses it, and the read-back probe cropping depends on. Node has no DOM, so the
// element and the probe canvas are the stubs from helpers/dom.js.
import test from 'node:test';
import assert from 'node:assert';

import { createStubElement, installDom } from './helpers/dom.js';

// A canvas whose getImageData throws exactly the way a tainted one does.
const canvasStub = (tainted) => createStubElement('canvas', {
  width: 0, height: 0,
  getContext: () => ({
    drawImage: () => {},
    getImageData: () => {
      if (tainted) throw new Error('The canvas has been tainted by cross-origin data.');
      return { data: new Uint8ClampedArray(4) };
    },
  }),
});

let tainted = false;
const doc = installDom({ createElement: (tag) => (tag === 'canvas' ? canvasStub(tainted) : createStubElement(tag)) });

const { loadMediaCors, retryWithoutCors, canReadPixels } = await import('../js/ui/canvas/mediaCors.js');

const media = () => createStubElement('img');

test('a remote source is loaded WITH the CORS ask, and remembers that it asked', () => {
  const el = media();
  loadMediaCors(el, 'https://cdn.example/photo.png', false);
  assert.strictEqual(el.crossOrigin, 'anonymous');
  assert.strictEqual(el.src, 'https://cdn.example/photo.png');
  assert.strictEqual(el.dataset.corsTry, '1', 'the ask is what makes a retry possible');
});

test('a same-origin source asks for nothing — and drops an ask left from before', () => {
  const el = media();
  loadMediaCors(el, 'https://cdn.example/photo.png', false);
  loadMediaCors(el, 'blob:local-copy', true);
  assert.ok(!el.hasAttribute('crossorigin'), 'the attribute is removed, not just blanked');
  assert.strictEqual(el.dataset.corsTry, '');
  assert.strictEqual(el.src, 'blob:local-copy');
});

test('the retry drops the ask and re-loads the same source, exactly once', () => {
  const el = media();
  loadMediaCors(el, 'https://cdn.example/photo.png', false);
  el.src = '';   // the failed load cleared it, as a real error would

  assert.strictEqual(retryWithoutCors(el, 'https://cdn.example/photo.png'), true);
  assert.ok(!el.hasAttribute('crossorigin'));
  assert.strictEqual(el.src, 'https://cdn.example/photo.png');
  assert.strictEqual(el.dataset.corsTry, '');

  // The PLAIN load is the one that just failed: the caller must report a real error now.
  assert.strictEqual(retryWithoutCors(el, 'https://cdn.example/photo.png'), false);
});

test('a same-origin load never retries — its failure is real the first time', () => {
  const el = media();
  loadMediaCors(el, 'blob:local-copy', true);
  assert.strictEqual(retryWithoutCors(el, 'blob:local-copy'), false);
});

test('canReadPixels answers the response, not the URL', () => {
  tainted = false;
  assert.strictEqual(canReadPixels(media()), true, 'a permitting host reads back');
  tainted = true;
  assert.strictEqual(canReadPixels(media()), false, 'a tainted canvas throws, and that is the answer');
  tainted = false;
});

test('the probe canvas is one pixel — the read is a permission check, not a copy', () => {
  const sizes = [];
  const probe = installDom({
    createElement: (tag) => {
      const c = canvasStub(false);
      if (tag === 'canvas') Object.defineProperty(c, 'width', {
        set(v) { sizes.push(v); }, get() { return sizes.at(-1) ?? 0; }, configurable: true,
      });
      return c;
    },
  });
  canReadPixels(media());
  assert.deepStrictEqual(sizes, [1], 'a bigger probe would decode the whole picture again');
  probe.restore();
  globalThis.document = doc;
});
