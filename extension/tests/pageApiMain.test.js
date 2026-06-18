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

// Install a fake page on globalThis. Returns the captured postMessage payloads.
const setupEnv = ({ imgs = [], bgs = [], videos = [], stencilPreset } = {}) => {
  const posted = [];
  const win = { addEventListener() {}, postMessage: (m) => posted.push(m) };
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
  return { posted, win };
};

let caseId = 0;
// Fresh fake page + a fresh module evaluation; returns { stencil, posted, win }.
const loadApi = async (opts) => {
  const env = setupEnv(opts);
  await import(`../src/content/pageApiMain.js?case=${++caseId}`);
  return { stencil: env.win.stencil, ...env };
};

const SRC = { PAGE_API: 'stencil-page-api' };
const MSG = { PAGE_OPEN: 'stencil-page-open', PAGE_CROP: 'stencil-page-crop', PAGE_DISABLE: 'stencil-page-disable', PAGE_SET_FILTERS: 'stencil-page-set-filters' };
const sent = (posted) => posted.filter((p) => p.source === SRC.PAGE_API).map((p) => p.message);

test('injects a tagged, non-enumerable, non-writable window.stencil', async () => {
  const { stencil, win } = await loadApi();
  assert.ok(stencil, 'window.stencil is defined');
  assert.equal(stencil.__stencil, 'page');
  assert.equal(stencil.enabled, true);

  const desc = Object.getOwnPropertyDescriptor(win, 'stencil');
  assert.equal(desc.enumerable, false);
  assert.equal(desc.writable, false);   // plain reassignment can't replace the binding
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
