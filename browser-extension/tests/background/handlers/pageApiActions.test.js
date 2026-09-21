// The action half of window.stencil (src/content/pageApiMain.js): open/crop requests posted to
// the ISOLATED bridge, the optimistic pin flip, and the pin/edited snapshots pushed back.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { img, loadApi, video, MSG, SRC, sent } from '../../helpers/pageApiEnv.js';

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

  // open() flags: newTab and desktop ride through for the SW to route.
  stencil.open('http://cdn/z.png', { newTab: true });
  assert.equal(sent(posted).at(-1).newTab, true);
  stencil.open('http://cdn/z.png', { desktop: true });
  const desktopOpen = sent(posted).at(-1);
  assert.equal(desktopOpen.type, MSG.PAGE_OPEN);
  assert.equal(desktopOpen.desktop, true);
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
