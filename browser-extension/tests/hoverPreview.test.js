// Tests for src/lib/hoverPreview.js — the rows' floating magnifier card extracted from
// popup.js: pure placement, the worthwhile gate, and the debounce / stale-token /
// tiny-memo machinery, driven with stub elements.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { previewPosition, previewWorthwhile, createHoverPreview } from '../src/lib/highlight/hoverPreview.js';

const tick = (ms = 5) => new Promise((r) => setTimeout(r, ms));

// ── Pure ──

test('previewPosition: right of the row, flipping left and clamping up on overflow', () => {
  const viewport = { width: 400, height: 600 };
  const size = { width: 200, height: 200 };
  // Fits to the right.
  assert.deepEqual(previewPosition({ anchor: { left: 10, right: 60, top: 50 }, size, viewport }),
    { left: 72, top: 50 });
  // No room right → flip to the left of the row.
  assert.deepEqual(previewPosition({ anchor: { left: 250, right: 300, top: 50 }, size, viewport }),
    { left: 250 - 200 - 12, top: 50 });
  // Bottom overflow → pulled up inside the viewport.
  assert.equal(previewPosition({ anchor: { left: 10, right: 60, top: 550 }, size, viewport }).top,
    600 - 200 - 12);
  // Nothing fits → pinned at the 12px margin, never negative.
  assert.deepEqual(previewPosition({ anchor: { left: 5, right: 10, top: 5 }, size: { width: 500, height: 700 }, viewport }),
    { left: 12, top: 12 });
});

test('previewWorthwhile: thumbnail-sized skips, unmeasured still previews', () => {
  assert.equal(previewWorthwhile({ w: 48, h: 48 }, 48), false);
  assert.equal(previewWorthwhile({ w: 49, h: 48 }, 48), true);
  assert.equal(previewWorthwhile({ w: 0, h: 0 }, 48), true, 'unmeasured (0×0) still gets a preview');
});

// ── Stub DOM ──
const stubEl = () => {
  const el = {
    hidden: true, style: {}, offsetWidth: 100, offsetHeight: 100,
    rect: { left: 10, right: 60, top: 50, width: 50, height: 20 },
    handlers: {},
    addEventListener: (t, fn) => { (el.handlers[t] || (el.handlers[t] = [])).push(fn); },
    getBoundingClientRect: () => el.rect,
    fire: async (t) => { for (const fn of el.handlers[t] || []) await fn(); },
  };
  return el;
};
const stubImg = () => {
  const img = stubEl();
  img.src = '';
  img.naturalWidth = 300;
  img.naturalHeight = 300;
  img.decodeError = false;
  img.decode = async () => { if (img.decodeError) throw new Error('undecodable'); };
  img.rect = { width: 100, height: 100 };
  return img;
};
const build = ({ fetch } = {}) => {
  const previewEl = stubEl();
  const previewImg = stubImg();
  const fetched = [];
  const hp = createHoverPreview({
    previewEl, previewImg, thumbPx: 48,
    fetchDataUrl: fetch || (async (src, pageUrl) => { fetched.push([src, pageUrl]); return `data:from-${src}`; }),
    getSrc: (image) => image.src || '',
    getPageUrl: (image) => image.resource || '',
    win: { innerWidth: 400, innerHeight: 600 },
    debounceMs: 0,
  });
  return { previewEl, previewImg, fetched, hp };
};

// ── bind (row hover) ──

test('hovering a row fetches through the injected path, caches, and shows the card', async () => {
  const { previewEl, previewImg, fetched, hp } = build();
  const row = stubEl();
  hp.bind(row, { src: 'http://x/a.png', resource: 'http://x/page', w: 0, h: 0 });
  await row.fire('mouseenter');
  await tick();
  assert.deepEqual(fetched, [['http://x/a.png', 'http://x/page']]);
  assert.equal(previewImg.src, 'data:from-http://x/a.png');
  assert.equal(previewEl.hidden, false);
  assert.equal(previewEl.style.left, '72px');   // right of the row, 12px gap
  assert.equal(hp.cache.get('http://x/a.png'), 'data:from-http://x/a.png');

  // Re-hover: served from the cache, no second fetch.
  await row.fire('mouseenter');
  await tick();
  assert.equal(fetched.length, 1);
});

test('a data: source is shown directly — never fetched', async () => {
  const { previewImg, fetched, hp } = build();
  const row = stubEl();
  hp.bind(row, { src: 'data:image/png;base64,xx', w: 0, h: 0 });
  await row.fire('mouseenter');
  await tick();
  assert.equal(fetched.length, 0);
  assert.equal(previewImg.src, 'data:image/png;base64,xx');
});

test('undecodable bytes memo the source as tiny; the next hover skips entirely', async () => {
  const { previewEl, previewImg, fetched, hp } = build();
  previewImg.decodeError = true;
  const row = stubEl();
  hp.bind(row, { src: 'http://x/broken.png', w: 0, h: 0 });
  await row.fire('mouseenter');
  await tick();
  assert.equal(previewEl.hidden, true);
  previewImg.decodeError = false;
  await row.fire('mouseenter');
  await tick();
  assert.equal(fetched.length, 1, 'the tiny memo skips the second attempt outright');
  assert.equal(previewEl.hidden, true);
});

test('thumbnail-sized bytes never show a card, and a collapsed render hides it again', async () => {
  const { previewEl, previewImg, hp } = build();
  previewImg.naturalWidth = 32;
  previewImg.naturalHeight = 32;
  const row = stubEl();
  hp.bind(row, { src: 'http://x/tiny.png', w: 0, h: 0 });
  await row.fire('mouseenter');
  await tick();
  assert.equal(previewEl.hidden, true);

  // A 0×0-intrinsic SVG renders collapsed — the bare padding pill is hidden.
  const { previewEl: el2, previewImg: img2, hp: hp2 } = build();
  img2.rect = { width: 10, height: 10 };
  const row2 = stubEl();
  hp2.bind(row2, { src: 'http://x/flat.svg', w: 0, h: 0 });
  await row2.fire('mouseenter');
  await tick();
  assert.equal(el2.hidden, true);
});

test('hide() cancels an in-flight fetch — the card never appears late', async () => {
  let release = null;
  const { previewEl, hp } = build({
    fetch: () => new Promise((r) => { release = () => r('data:late'); }),
  });
  const row = stubEl();
  hp.bind(row, { src: 'http://x/slow.png', w: 0, h: 0 });
  await row.fire('mouseenter');
  await tick();
  hp.hide();               // pointer moved on (or a drag started)
  release();
  await tick();
  assert.equal(previewEl.hidden, true);
});

test('a source too small to beat the thumbnail never schedules at all', async () => {
  const { fetched, hp } = build();
  const row = stubEl();
  hp.bind(row, { src: 'http://x/48.png', w: 48, h: 48 });
  await row.fire('mouseenter');
  await tick();
  assert.equal(fetched.length, 0);
});

// ── bindDataUrl (already-bytes hover, the editor rows' canvas capture) ──

test('bindDataUrl shows the small capture at once, then upgrades to bigger()', async () => {
  const { previewEl, previewImg, hp } = build();
  const el = stubEl();
  let resolveBig = null;
  hp.bindDataUrl(el, 'data:small', () => new Promise((r) => { resolveBig = r; }));
  await el.fire('mouseenter');
  await tick();
  assert.equal(previewImg.src, 'data:small');
  assert.equal(previewEl.hidden, false);
  resolveBig('data:big');
  await tick();
  assert.equal(previewImg.src, 'data:big');
});

test('a hide() between small and bigger() drops the late upgrade', async () => {
  const { previewImg, hp } = build();
  const el = stubEl();
  let resolveBig = null;
  hp.bindDataUrl(el, 'data:small', () => new Promise((r) => { resolveBig = r; }));
  await el.fire('mouseenter');
  await tick();
  hp.hide();
  resolveBig('data:big');
  await tick();
  assert.equal(previewImg.src, 'data:small', 'the stale upgrade lost the token race');
});

// ── The img load listener ──

test('a load reporting thumbnail-sized dimensions hides the card and memos the source', async () => {
  const { previewEl, previewImg, hp } = build();
  const row = stubEl();
  hp.bind(row, { src: 'http://x/a.png', w: 0, h: 0 });
  await row.fire('mouseenter');
  await tick();
  assert.equal(previewEl.hidden, false);
  previewImg.naturalWidth = 20;
  previewImg.naturalHeight = 20;
  await previewImg.fire('load');
  assert.equal(previewEl.hidden, true);
  // …and the memo suppresses the next hover.
  await row.fire('mouseenter');
  await tick();
  assert.equal(previewEl.hidden, true);
});
