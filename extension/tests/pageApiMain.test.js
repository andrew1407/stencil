// Tests for the page-global window.stencil scripting API (src/content/pageApiMain.js).
// That file is a MAIN-world IIFE that scans the page's images/videos and posts action
// requests to the ISOLATED bridge — it has no exports, so we install a fake DOM/window
// on globalThis and import the module for its side effect (it defines window.stencil).
// Each scenario re-imports with a unique ?case= query so node re-evaluates the IIFE
// against a fresh fake page (ESM caches by specifier; the query busts that cache).
//
// Mirrors the documented surface in extension/README.md ("Page scripting API"). The
// video-frame capture path of open()/crop() needs a real <canvas>/decoder, so it's
// exercised only via the poster fallback here; live frame grabbing is browser-only.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// A fake <img>: scan() reads currentSrc / getAttribute('src'); entryDims reads naturalWidth.
const img = (url, { naturalWidth = 100, naturalHeight = 80 } = {}) => ({
  nodeType: 1, tagName: 'IMG', currentSrc: url, naturalWidth, naturalHeight,
  offsetWidth: naturalWidth, offsetHeight: naturalHeight,
  getAttribute: (a) => (a === 'src' ? url : null),
  setAttribute() {}, removeAttribute() {},
});
// A fake element carrying a CSS background-image (resolved via getComputedStyle below).
const bg = (url, { offsetWidth = 300, offsetHeight = 150 } = {}) => ({
  nodeType: 1, tagName: 'DIV', _bg: url, offsetWidth, offsetHeight,
  getAttribute: () => null, setAttribute() {}, removeAttribute() {},
});
// A fake <video>: poster declared, no decodable frame (videoWidth 0) → poster is used.
const video = (src, poster, { videoWidth = 0, videoHeight = 0 } = {}) => ({
  nodeType: 1, tagName: 'VIDEO', currentSrc: src, src, videoWidth, videoHeight,
  readyState: 0, paused: true, currentTime: 0,
  getAttribute: (a) => (a === 'poster' ? poster : a === 'src' ? src : null),
  setAttribute() {}, removeAttribute() {},
});

// Install a fake page on globalThis. Returns the captured postMessage payloads and a
// `dispatch` that delivers a window 'message' to the API (as the ISOLATED bridge would).
const setupEnv = ({ imgs = [], bgs = [], videos = [], stencilPreset } = {}) => {
  const posted = [];
  const listeners = [];
  const win = {
    addEventListener(type, fn) { if (type === 'message') listeners.push(fn); },
    postMessage: (m) => posted.push(m),
  };
  // The API's handler checks `e.source === window` (= win) — mimic a real same-window message.
  const dispatch = (data) => { for (const fn of listeners) fn({ source: win, data }); };
  if (stencilPreset !== undefined) win.stencil = stencilPreset;
  globalThis.window = win;
  globalThis.location = { href: 'http://page.example/here' };
  // Resolve a background URL only for the elements we tagged with _bg.
  globalThis.getComputedStyle = (el) => ({ backgroundImage: el && el._bg ? `url("${el._bg}")` : '' });
  const all = [...imgs, ...videos, ...bgs];
  globalThis.document = {
    querySelectorAll: (sel) => (sel === 'img' ? imgs : sel === 'video' ? videos : sel === '*' ? all : []),
    getElementById: () => null,
    createElement: () => ({ getContext: () => null, style: {}, setAttribute() {} }),
    head: { appendChild() {} },
    documentElement: { appendChild() {} },
  };
  return { posted, win, dispatch };
};

let caseId = 0;
// Fresh fake page + a fresh module evaluation; returns { stencil, posted, win, dispatch }.
const loadApi = async (opts) => {
  const env = setupEnv(opts);
  await import(`../src/content/pageApiMain.js?case=${++caseId}`);
  return { stencil: env.win.stencil, ...env };
};

const SRC = { PAGE_API: 'stencil-page-api', PAGE_PINS: 'stencil-page-pins', PAGE_EDITED: 'stencil-page-edited' };
const MSG = { PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_PIN: 'stencil-page-pin', PAGE_REQUEST_SYNC: 'stencil-page-request-sync', PAGE_DISABLE: 'stencil-page-disable', PAGE_SET_FILTERS: 'stencil-page-set-filters' };
// Posted API messages. The IIFE fires one PAGE_REQUEST_SYNC on load (asking the bridge to
// push current state); filter it out so tests assert on the messages their actions caused.
const sent = (posted) => posted.filter((p) => p.source === SRC.PAGE_API).map((p) => p.message).filter((m) => m.type !== MSG.PAGE_REQUEST_SYNC);

test('injects a tagged, non-enumerable, non-writable window.stencil', async () => {
  const { stencil, win } = await loadApi();
  assert.ok(stencil, 'window.stencil is defined');
  assert.equal(stencil.__stencil, 'page');
  assert.equal(stencil.enabled, true);

  const desc = Object.getOwnPropertyDescriptor(win, 'stencil');
  assert.equal(desc.enumerable, false);
  assert.equal(desc.writable, false);   // plain reassignment can't replace the binding
});

test('on load the API requests a state sync from the bridge (avoids the document_start race)', async () => {
  const { posted } = await loadApi();
  const types = posted.filter((p) => p.source === SRC.PAGE_API).map((p) => p.message.type);
  assert.ok(types.includes(MSG.PAGE_REQUEST_SYNC));
});

test('the API object is a hard read-only proxy; only `enabled=false` writes through', async () => {
  const { stencil, posted } = await loadApi();

  assert.throws(() => { stencil.open = 0; }, /read-only/);
  assert.throws(() => { stencil.__stencil = 'x'; }, /read-only/);
  assert.throws(() => { delete stencil.items; }, /cannot be deleted/);

  stencil.enabled = false;   // the one legit setter → posts a disable request
  assert.deepEqual(sent(posted).map((m) => m.type), [MSG.PAGE_DISABLE]);
});

test('no-clobber guard: leaves an existing window.stencil (the editor or a prior inject) alone', async () => {
  // Editor's own API: present, but carries no __stencil tag → must be left untouched.
  const editor = { iAmTheEditor: true };
  const { win: w1 } = await loadApi({ stencilPreset: editor });
  assert.equal(w1.stencil, editor);

  // A prior page-API inject (tagged 'page') → also a no-op (no double-inject).
  const prior = { __stencil: 'page' };
  const { win: w2 } = await loadApi({ stencilPreset: prior });
  assert.equal(w2.stencil, prior);
});

test('list getters split the scan by kind: items / images / backgrounds / videos / posters', async () => {
  const { stencil } = await loadApi({
    imgs: [img('http://cdn/a.png')],
    bgs: [bg('http://cdn/hero.jpg')],
    videos: [video('http://cdn/clip.mp4', 'http://cdn/poster.webp')],
  });

  assert.equal(stencil.items.length, 3);                                  // image + background + video
  assert.deepEqual(stencil.images.map((e) => e.url), ['http://cdn/a.png']);
  assert.deepEqual(stencil.backgrounds.map((e) => e.kind), ['background']);
  assert.deepEqual(stencil.videos.map((e) => e.kind), ['video']);
  assert.deepEqual(stencil.posters.map((e) => ({ url: e.url, poster: e.poster })),
    [{ url: 'http://cdn/poster.webp', poster: true }]);
});

test('scanned entry exposes element/kind/url/name/format/width/height', async () => {
  const { stencil } = await loadApi({ imgs: [img('http://cdn/photo.png', { naturalWidth: 640, naturalHeight: 480 })] });
  const e = stencil.items[0];
  assert.equal(e.kind, 'image');
  assert.equal(e.url, 'http://cdn/photo.png');
  assert.equal(e.name, 'photo.png');
  assert.equal(e.format, 'png');
  assert.equal(e.width, 640);
  assert.equal(e.height, 480);
  assert.equal(e.element.tagName, 'IMG');
  assert.throws(() => { e.url = 'x'; }, /read-only/);   // entries are guarded too
});

test('kind, format, search-text, and size-bound filters narrow the list getters', async () => {
  const { stencil } = await loadApi({
    imgs: [img('http://cdn/cat.png', { naturalWidth: 100 }), img('http://cdn/dog.jpg', { naturalWidth: 900 })],
  });
  assert.equal(stencil.items.length, 2);

  stencil.formats.png = false;                       // per-format toggle
  assert.deepEqual(stencil.items.map((e) => e.format), ['jpg']);
  stencil.formats.png = true;

  stencil.searchText = 'cat';                         // name/URL substring
  assert.deepEqual(stencil.items.map((e) => e.name), ['cat.png']);
  stencil.searchText = '';

  stencil.minWidth = 500;                             // size bound
  assert.deepEqual(stencil.items.map((e) => e.name), ['dog.jpg']);
  stencil.minWidth = null;

  stencil.kinds.image = false;                        // per-kind toggle
  assert.equal(stencil.items.length, 0);
});

test('resetFilters clears every filter and returns the facade', async () => {
  const { stencil, posted } = await loadApi({ imgs: [img('http://cdn/a.png'), img('http://cdn/b.jpg')] });
  stencil.searchText = 'zzz'; stencil.kinds.image = false; stencil.minWidth = 9999;
  assert.equal(stencil.items.length, 0);

  assert.equal(stencil.resetFilters(), stencil);
  assert.equal(stencil.items.length, 2);
  assert.equal(stencil.searchText, '');
  assert.equal(stencil.minWidth, null);
  // Filter mutations are persisted to the popup (PAGE_SET_FILTERS posts).
  assert.ok(sent(posted).some((m) => m.type === MSG.PAGE_SET_FILTERS));
});

test('search()/format()/size() are one-off queries that ignore the live filters', async () => {
  const { stencil } = await loadApi({
    imgs: [img('http://cdn/cat.png', { naturalWidth: 100 }), img('http://cdn/dog.jpg', { naturalWidth: 900 })],
  });

  stencil.kinds.image = false;            // would empty the list getters...
  assert.equal(stencil.search('cat').length, 1);   // ...but the ad-hoc queries ignore filters
  assert.equal(stencil.search('cat')[0].url, 'http://cdn/cat.png');
  assert.equal(stencil.format('.jpg').length, 1);
  assert.equal(stencil.format('jpg')[0].name, 'dog.jpg');
  assert.deepEqual(stencil.size({ minW: 500 }).map((e) => e.name), ['dog.jpg']);
});

test('highlightOnPage get/set reflects the shared highlight style element', async () => {
  // Track the injected <style id=stencil-hl-style> so highlightActive() can see it.
  const styles = new Map();
  const { stencil } = await (async () => {
    const env = setupEnv({ imgs: [img('http://cdn/a.png')] });
    env.win;
    globalThis.document.getElementById = (id) => styles.get(id) || null;
    globalThis.document.createElement = () => {
      const el = { id: '', set textContent(_) {}, remove() { styles.delete(el.id); } };
      return el;
    };
    globalThis.document.head.appendChild = (el) => styles.set(el.id, el);
    globalThis.document.querySelectorAll = ((orig) => (sel) =>
      sel === '[data-stencil-hl]' ? [] : orig(sel))(globalThis.document.querySelectorAll);
    await import(`../src/content/pageApiMain.js?case=${++caseId}`);
    return { stencil: env.win.stencil };
  })();

  assert.equal(stencil.highlightOnPage, false);
  stencil.highlightOnPage = true;
  assert.equal(stencil.highlightOnImage, true);   // alias reads the same state
  stencil.highlightOnPage = false;
  assert.equal(stencil.highlightOnPage, false);
});

test('open()/crop() resolve a URL/element target and post the right request', async () => {
  const { stencil, posted } = await loadApi();

  // String URL target.
  assert.equal(stencil.open('http://cdn/x.png'), stencil);   // chainable
  const open = sent(posted).at(-1);
  assert.equal(open.type, MSG.PAGE_OPEN);
  assert.equal(open.url, 'http://cdn/x.png');
  assert.equal(open.name, 'x.png');
  assert.equal(open.source, 'http://cdn/x.png');
  assert.equal(open.resource, 'http://page.example/here');   // location.href

  // Element target → crop request, with the album flag threaded through.
  stencil.crop(img('http://cdn/y.png'), { album: true });
  const crop = sent(posted).at(-1);
  assert.equal(crop.type, MSG.PAGE_CROP);
  assert.equal(crop.url, 'http://cdn/y.png');
  assert.equal(crop.album, true);
});

test('entry.open()/entry.crop() act on the scanned entry; video falls back to its poster', async () => {
  const { stencil, posted } = await loadApi({
    imgs: [img('http://cdn/a.png')],
    videos: [video('http://cdn/clip.mp4', 'http://cdn/poster.webp')],
  });

  stencil.images[0].open({ incognito: true });
  let m = sent(posted).at(-1);
  assert.equal(m.type, MSG.PAGE_OPEN);
  assert.equal(m.url, 'http://cdn/a.png');
  assert.equal(m.incognito, true);

  // A video with no decodable frame opens its poster (the { poster:true } path).
  stencil.videos[0].open({ poster: true });
  m = sent(posted).at(-1);
  assert.equal(m.url, 'http://cdn/poster.webp');
});

test('open() throws when handed something with no loadable image', async () => {
  const { stencil } = await loadApi();
  assert.throws(() => stencil.open(123), /pass an image\/video element/);
  assert.throws(() => stencil.open({ nodeType: 1, tagName: 'DIV', getAttribute: () => null }), /not a loadable image/);
});

test('entry.pinned set/get posts PAGE_PIN and optimistically flips; pin()/unpin() chain', async () => {
  const { stencil, posted } = await loadApi({ imgs: [img('http://cdn/a.png')] });
  assert.equal(stencil.items[0].pinned, false);

  stencil.items[0].pinned = true;
  assert.equal(stencil.items[0].pinned, true);   // optimistic local flip (entries are re-scanned, state is by URL)
  let m = sent(posted).at(-1);
  assert.equal(m.type, MSG.PAGE_PIN);
  assert.equal(m.pin, true);
  assert.equal(m.url, 'http://cdn/a.png');
  assert.equal(m.source, 'http://cdn/a.png');
  assert.equal(m.name, 'a.png');

  const e = stencil.items[0];
  assert.equal(e.unpin(), e);                     // chainable
  assert.equal(sent(posted).at(-1).pin, false);
  assert.equal(stencil.items[0].pinned, false);

  const e2 = stencil.items[0];
  assert.equal(e2.pin(), e2);                     // pin() returns the entry (chainable)
  assert.equal(sent(posted).at(-1).pin, true);
});

test('entry.isEdited reflects the pushed opened-sources snapshot and is read-only', async () => {
  const { stencil, dispatch } = await loadApi({ imgs: [img('http://cdn/a.png')] });
  assert.equal(stencil.items[0].isEdited, false);
  assert.throws(() => { stencil.items[0].isEdited = true; }, /read-only/);

  dispatch({ source: SRC.PAGE_EDITED, sources: ['http://cdn/a.png'] });
  assert.equal(stencil.items[0].isEdited, true);
});

test('a pushed PAGE_PINS snapshot drives entry.pinned and stencil.pins (bridge → MAIN sync)', async () => {
  const { stencil, dispatch } = await loadApi({ imgs: [img('http://cdn/a.png'), img('http://cdn/b.png')] });
  assert.deepEqual(stencil.pins.map((e) => e.url), []);

  dispatch({ source: SRC.PAGE_PINS, sources: ['http://cdn/b.png'] });
  assert.equal(stencil.images[0].pinned, false);
  assert.equal(stencil.images[1].pinned, true);
  assert.deepEqual(stencil.pins.map((e) => e.url), ['http://cdn/b.png']);

  dispatch({ source: SRC.PAGE_PINS, sources: [] });   // unpinned elsewhere
  assert.deepEqual(stencil.pins.map((e) => e.url), []);
});

test('stencil.pin accepts an index, URL, element, or array; unpin posts pin:false', async () => {
  const { stencil, posted } = await loadApi({ imgs: [img('http://cdn/a.png'), img('http://cdn/b.png')] });

  assert.equal(stencil.pin(0), stencil);                  // chainable; index into stencil.items
  assert.equal(sent(posted).at(-1).url, 'http://cdn/a.png');
  assert.equal(sent(posted).at(-1).pin, true);

  stencil.pin('http://cdn/z.png');                        // URL string
  assert.equal(sent(posted).at(-1).url, 'http://cdn/z.png');

  stencil.pin(img('http://cdn/c.png'));                   // DOM element
  assert.equal(sent(posted).at(-1).url, 'http://cdn/c.png');

  stencil.pin([0, 1]);                                    // array → one post per target
  assert.deepEqual(sent(posted).slice(-2).map((m) => m.url), ['http://cdn/a.png', 'http://cdn/b.png']);

  stencil.unpin(0);
  assert.equal(sent(posted).at(-1).pin, false);
});

test('stencil.pin throws on an unpinnable target', async () => {
  const { stencil } = await loadApi();
  assert.throws(() => stencil.pin({}), /expects an entry/);
});

test('stencil.detect describes an entry/index/element/URL target (or null), never throwing', async () => {
  const { stencil } = await loadApi({
    imgs: [img('http://cdn/a.png', { naturalWidth: 64, naturalHeight: 48 })],
    videos: [video('http://cdn/clip.mp4', 'http://cdn/poster.webp')],
  });

  // By index into stencil.items.
  const d = stencil.detect(0);
  assert.equal(d.kind, 'image');
  assert.equal(d.url, 'http://cdn/a.png');
  assert.equal(d.name, 'a.png');
  assert.equal(d.format, 'png');
  assert.equal(d.element.tagName, 'IMG');
  assert.equal(d.listed, true);          // appears in stencil.items
  assert.equal(d.pinned, false);

  // The same entry object.
  assert.equal(stencil.detect(stencil.items[0]).url, 'http://cdn/a.png');

  // A raw element NOT on the page → still described, but not listed.
  const off = stencil.detect(img('http://cdn/off.jpg'));
  assert.equal(off.kind, 'image');
  assert.equal(off.listed, false);

  // A bare URL → kind inferred from the extension; videos read as 'video'.
  assert.equal(stencil.detect('http://cdn/movie.mp4').kind, 'video');
  assert.equal(stencil.detect('http://cdn/pic.png').kind, 'image');

  // A <video> with a poster but no decodable frame: described, hasPoster flagged.
  const v = stencil.detect(stencil.videos[0]);
  assert.equal(v.kind, 'video');
  assert.equal(v.hasPoster, true);
  assert.equal(v.hasFrame, false);

  // Non-targets → null (no throw).
  assert.equal(stencil.detect({ nodeType: 1, tagName: 'DIV', getAttribute: () => null }), null);
  assert.equal(stencil.detect(999), null);   // out-of-range index
  assert.equal(stencil.detect(null), null);
  assert.equal(stencil.detect({}), null);
});

test('stencil.grabbable is the boolean pre-check for a pin/open target', async () => {
  const { stencil } = await loadApi({ imgs: [img('http://cdn/a.png')] });
  assert.equal(stencil.grabbable(0), true);
  assert.equal(stencil.grabbable(stencil.items[0]), true);
  assert.equal(stencil.grabbable(img('http://cdn/b.png')), true);
  assert.equal(stencil.grabbable('http://cdn/c.png'), true);
  assert.equal(stencil.grabbable({ nodeType: 1, tagName: 'DIV', getAttribute: () => null }), false);
  assert.equal(stencil.grabbable(999), false);
  assert.equal(stencil.grabbable(undefined), false);
});

test('detect reports the live pinned/listed state', async () => {
  const { stencil, dispatch } = await loadApi({ imgs: [img('http://cdn/a.png'), img('http://cdn/b.png')] });
  dispatch({ source: SRC.PAGE_PINS, sources: ['http://cdn/b.png'] });
  assert.equal(stencil.detect('http://cdn/a.png').pinned, false);
  assert.equal(stencil.detect('http://cdn/b.png').pinned, true);

  stencil.searchText = 'a';                                  // filter b.png out of the list
  assert.equal(stencil.detect('http://cdn/b.png').listed, false);
  assert.equal(stencil.detect('http://cdn/a.png').listed, true);
});
