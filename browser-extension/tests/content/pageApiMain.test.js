// The page-global window.stencil scripting API (src/content/pageApiMain.js), a MAIN-world IIFE:
// what it injects, how the scan splits by kind, and how the live filters narrow the list getters.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { bg, img, importApi, loadApi, setupEnv, video, MSG, SRC, sent } from '../helpers/pageApiEnv.js';

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

test('regex flag: live filter and search() treat the query as a case-insensitive RegExp', async () => {
  const { stencil } = await loadApi({
    imgs: [img('http://cdn/cat.png'), img('http://cdn/dog.jpg')],
  });

  // Live filter: stencil.regex makes searchText an anchored regex over "name url".
  stencil.regex = true;
  stencil.searchText = '\\.jpg';
  assert.deepEqual(stencil.items.map((e) => e.name), ['dog.jpg']);
  stencil.searchText = 'CAT';                     // case-insensitive
  assert.deepEqual(stencil.items.map((e) => e.name), ['cat.png']);
  stencil.searchText = 'cat(';                    // invalid regex → nothing matches
  assert.equal(stencil.items.length, 0);
  stencil.resetFilters();
  assert.equal(stencil.regex, false);

  // One-off search(): opt-in regex, default stays substring.
  assert.deepEqual(stencil.search('\\.png$', { regex: true }).map((e) => e.name), ['cat.png']);
  assert.equal(stencil.search('\\.png$').length, 0);   // literal substring absent
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
    await importApi();
    return { stencil: env.win.stencil };
  })();

  assert.equal(stencil.highlightOnPage, false);
  stencil.highlightOnPage = true;
  assert.equal(stencil.highlightOnImage, true);   // alias reads the same state
  stencil.highlightOnPage = false;
  assert.equal(stencil.highlightOnPage, false);
});
