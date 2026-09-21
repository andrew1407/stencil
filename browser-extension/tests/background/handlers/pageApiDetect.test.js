// stencil.detect / stencil.grabbable (src/content/pageApiMain.js): describing any target without
// ever throwing, and reporting its live pinned/listed state.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { img, loadApi, video, SRC } from '../../helpers/pageApiEnv.js';

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
