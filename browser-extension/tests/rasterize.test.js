// Tests for src/lib/rasterize.js — the decode path every assistant attachment goes
// through. The bug it exists for: Chrome's createImageBitmap REFUSES an
// `image/svg+xml` blob ("The source image could not be decoded"), so dragging an SVG
// row (Wikipedia badges, logos, icons) into the chat failed even though the popup
// could show a thumbnail for it. SVG must be rasterised to PNG anyway — contract §7
// accepts png/jpeg/webp/gif only.
// Every DOM seam is injected, so this runs under plain `node --test`: no DOM, no
// canvas, no fetch.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  DEFAULT_MAX_EDGE, DEFAULT_RASTER_EDGE, DECODE_ERROR,
  isSvgType, isSvgUrl, mediaTypeOf, fitSize, rasterSize,
  rasterizeToPngDataUrl, decodeSize,
} from '../src/lib/rasterize.js';

// ── Test doubles ──

const SVG_DATA_URL = 'data:image/svg+xml;base64,' + Buffer.from('<svg viewBox="0 0 10 10"/>').toString('base64');
const PNG_DATA_URL = 'data:image/png;base64,AAAA';

// A canvas that records the size it was set to and what was drawn into it; its
// toDataURL encodes the size so the PNG payload is assertable.
const stubCanvas = (state) => {
  const c = {
    width: 0, height: 0, drawn: null,
    getContext: () => ({ drawImage: (src, x, y, w, h) => { c.drawn = { src, x, y, w, h }; } }),
    toDataURL: (type) => `data:${type};base64,` + Buffer.from(`${c.width}x${c.height}`).toString('base64'),
  };
  state.canvases.push(c);
  return c;
};

const canvasSize = (dataUrl) => Buffer.from(dataUrl.split(',')[1], 'base64').toString();

// An <img> stand-in: assigning .src resolves (or rejects) on the microtask queue.
const stubImageFactory = (state, { naturalWidth = 0, naturalHeight = 0, fail = false } = {}) => () => {
  const handlers = {};
  const img = {
    width: 0, height: 0, decoding: '', naturalWidth, naturalHeight, src: '',
    addEventListener: (t, fn) => { (handlers[t] || (handlers[t] = [])).push(fn); },
  };
  Object.defineProperty(img, 'src', {
    get: () => img._src,
    set: (v) => {
      img._src = v;
      queueMicrotask(() => { for (const fn of handlers[fail ? 'error' : 'load'] || []) fn(); });
    },
  });
  state.images.push(img);
  return img;
};

const baseDeps = (state, imgOpts) => ({
  createBitmap: null,
  makeImage: stubImageFactory(state, imgOpts),
  makeCanvas: () => stubCanvas(state),
  toBlob: async () => { state.blobFetches++; return { type: 'image/png' }; },
  objectUrl: (blob) => { state.objectUrls.push(blob); return 'blob:stub-1'; },
  revokeUrl: (u) => state.revoked.push(u),
  timer: () => 0,   // never fires — no test relies on the decode timeout
});

const newState = () => ({ canvases: [], images: [], objectUrls: [], revoked: [], blobFetches: 0, bitmapCalls: 0 });

// ── Pure helpers ──

test('isSvgType / isSvgUrl recognise every SVG spelling', () => {
  assert.equal(isSvgType('image/svg+xml'), true);
  assert.equal(isSvgType('image/svg+xml; charset=utf-8'), true);
  assert.equal(isSvgType('IMAGE/SVG'), true);
  assert.equal(isSvgType('image/png'), false);
  assert.equal(isSvgType(''), false);
  assert.equal(isSvgUrl('https://a.com/logo.svg?v=2'), true);
  assert.equal(isSvgUrl('https://a.com/logo.svgz'), true);
  assert.equal(isSvgUrl('data:image/svg+xml,<svg/>'), true);
  assert.equal(isSvgUrl('https://a.com/cat.png'), false);
});

test('mediaTypeOf reads a data: URL declaration only', () => {
  assert.equal(mediaTypeOf(SVG_DATA_URL), 'image/svg+xml');
  assert.equal(mediaTypeOf('data:image/svg+xml,<svg/>'), 'image/svg+xml');
  assert.equal(mediaTypeOf('https://a.com/x.png'), '');
});

test('fitSize scales the long edge down, never up', () => {
  assert.deepEqual(fitSize(3000, 1500, 1568), { width: 1568, height: 784 });
  assert.deepEqual(fitSize(100, 200, 1568), { width: 100, height: 200 });   // no upscale
  assert.deepEqual(fitSize(0, 0, 1568), { width: 0, height: 0 });
  assert.deepEqual(fitSize(null, 10, 1568), { width: 0, height: 0 });
});

test('rasterSize: known dims win, then natural, then the square fallback', () => {
  assert.deepEqual(rasterSize({ width: 800, height: 600, naturalWidth: 40, naturalHeight: 30 }), { width: 800, height: 600 });
  assert.deepEqual(rasterSize({ naturalWidth: 40, naturalHeight: 30 }), { width: 40, height: 30 });
  assert.deepEqual(rasterSize({}), { width: DEFAULT_RASTER_EDGE, height: DEFAULT_RASTER_EDGE });
  // Everything stays under the contract's 1568 px long edge.
  assert.deepEqual(rasterSize({ width: 4000, height: 2000, maxEdge: DEFAULT_MAX_EDGE }), { width: 1568, height: 784 });
});

test('rasterSize upscales a VECTOR source to minEdge (a 24px SVG is useless for vision)', () => {
  assert.deepEqual(rasterSize({ width: 24, height: 12, minEdge: 512 }), { width: 512, height: 256 });
  // Already big enough → untouched; and minEdge never beats maxEdge.
  assert.deepEqual(rasterSize({ width: 900, height: 900, minEdge: 512 }), { width: 900, height: 900 });
  assert.deepEqual(rasterSize({ width: 10, height: 10, minEdge: 4000, maxEdge: 1568 }), { width: 1568, height: 1568 });
});

// ── SVG rasterisation (the reported bug) ──

test('an SVG data URL never reaches createImageBitmap and rasterises via <img> + canvas', async () => {
  const state = newState();
  const deps = {
    ...baseDeps(state),
    createBitmap: async () => { state.bitmapCalls++; throw new Error('The source image could not be decoded.'); },
  };
  const out = await rasterizeToPngDataUrl({ dataUrl: SVG_DATA_URL }, { deps });

  assert.equal(state.bitmapCalls, 0, 'SVG must skip the bitmap decoder entirely');
  assert.ok(out.startsWith('data:image/png;base64,'), 'SVG must be rasterised to PNG, never sent as-is');
  // No intrinsic size and no known dims → the 512px default, set on the ELEMENT too
  // (an SVG with only a viewBox has nothing to lay out against).
  assert.equal(canvasSize(out), '512x512');
  assert.equal(state.images[0].width, 512);
  assert.equal(state.images[0].height, 512);
  assert.equal(state.images[0].src, SVG_DATA_URL);
});

test('a listed SVG rasterises at its scanned dimensions, upscaled to a usable size', async () => {
  const state = newState();
  const out = await rasterizeToPngDataUrl(
    { dataUrl: SVG_DATA_URL, width: 40, height: 20 }, { deps: baseDeps(state) });
  assert.equal(canvasSize(out), '512x256');
});

test('an SVG that reports an intrinsic size uses it', async () => {
  const state = newState();
  const deps = baseDeps(state, { naturalWidth: 1200, naturalHeight: 600 });
  const out = await rasterizeToPngDataUrl({ dataUrl: SVG_DATA_URL }, { deps });
  assert.equal(canvasSize(out), '1200x600');
});

// ── Bitmap path + fallback ──

test('a normal raster image goes through createImageBitmap and is downscaled to maxEdge', async () => {
  const state = newState();
  const deps = {
    ...baseDeps(state),
    createBitmap: async () => { state.bitmapCalls++; return { width: 3000, height: 1500, close: () => { state.closed = true; } }; },
  };
  const out = await rasterizeToPngDataUrl({ dataUrl: PNG_DATA_URL }, { deps, maxEdge: 1568 });
  assert.equal(state.bitmapCalls, 1);
  assert.equal(state.images.length, 0, 'no element decode needed');
  assert.equal(canvasSize(out), '1568x784');
  assert.equal(state.closed, true);
});

test('a createImageBitmap failure falls back to the element decode (opaque/odd sources)', async () => {
  const state = newState();
  const deps = {
    ...baseDeps(state, { naturalWidth: 300, naturalHeight: 200 }),
    createBitmap: async () => { state.bitmapCalls++; throw new Error('The source image could not be decoded.'); },
  };
  const out = await rasterizeToPngDataUrl({ dataUrl: PNG_DATA_URL }, { deps });
  assert.equal(state.bitmapCalls, 1);
  assert.equal(state.images.length, 1);
  assert.equal(canvasSize(out), '300x200');
});

test('when nothing can decode it, a clear error surfaces', async () => {
  const state = newState();
  const deps = {
    ...baseDeps(state, { fail: true }),
    createBitmap: async () => { throw new Error('nope'); },
  };
  await assert.rejects(
    () => rasterizeToPngDataUrl({ dataUrl: PNG_DATA_URL }, { deps }),
    (err) => err.message === DECODE_ERROR);
});

test('a Blob source is decoded through an object URL, which is revoked afterwards', async () => {
  const state = newState();
  const blob = { type: 'image/svg+xml' };          // SVG blob → element path
  const deps = baseDeps(state, { naturalWidth: 64, naturalHeight: 64 });
  const out = await rasterizeToPngDataUrl({ blob }, { deps });
  assert.deepEqual(state.objectUrls, [blob]);
  assert.deepEqual(state.revoked, ['blob:stub-1']);
  assert.equal(state.images[0].src, 'blob:stub-1');
  assert.equal(canvasSize(out), '512x512');        // 64px vector → upscaled to the default edge
});

test('rasterizeToPngDataUrl refuses an empty source', async () => {
  await assert.rejects(() => rasterizeToPngDataUrl({}, { deps: baseDeps(newState()) }), /no image source/);
});

// ── decodeSize (used by `open` to translate crop specs) ──

test('decodeSize measures via the bitmap decoder, and via <img> for SVG', async () => {
  const state = newState();
  const bitmapDeps = {
    ...baseDeps(state),
    createBitmap: async () => ({ width: 640, height: 480, close: () => {} }),
  };
  assert.deepEqual(await decodeSize({ dataUrl: PNG_DATA_URL }, { deps: bitmapDeps }), { width: 640, height: 480 });

  const svgState = newState();
  const svgDeps = {
    ...baseDeps(svgState, { naturalWidth: 128, naturalHeight: 64 }),
    createBitmap: async () => { throw new Error('The source image could not be decoded.'); },
  };
  assert.deepEqual(await decodeSize({ dataUrl: SVG_DATA_URL }, { deps: svgDeps }), { width: 128, height: 64 });
  assert.equal(svgState.images.length, 1);
});

// ── SSRF guard (lib/urlGuard.js) ──
// NEGATIVE: a page-harvested source naming a private/internal host is refused before
// EITHER decoder touches the network (fetch, then <img src>); a public one still decodes.
test('a private or internal source URL is refused before any decode', async () => {
  for (const url of ['http://127.0.0.1/x.png', 'http://10.0.0.5/x.png', 'http://169.254.169.254/latest/',
    'http://[::1]/x.png', 'http://localhost:8080/x.png', 'file:///etc/passwd']) {
    const state = newState();
    await assert.rejects(() => rasterizeToPngDataUrl({ dataUrl: url }, { deps: baseDeps(state) }), /blocked/, url);
    await assert.rejects(() => decodeSize({ dataUrl: url }, { deps: baseDeps(state) }), /blocked/, url);
    assert.equal(state.blobFetches, 0, url);
    assert.equal(state.images.length, 0, url);
  }
  const ok = await rasterizeToPngDataUrl({ dataUrl: 'https://cdn.example/x.png' }, { deps: baseDeps(newState()) });
  assert.match(ok, /^data:image\/png;base64,/);
});
