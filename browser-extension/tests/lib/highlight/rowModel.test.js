// Tests for src/lib/rowModel.js — what a scanned image's list row shows (tooltip,
// initial thumbnail source, badge pills, outline colour), extracted from renderRow.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { rowTitle, thumbInitialSrc, dimText, rowBadges, rowOutlineClass } from '../../../src/lib/rowModel.js';
import { UNKNOWN_FORMAT } from '../../../src/lib/highlight/filters.js';

// ── rowTitle ──

test('rowTitle: source + kind + gesture hint; a data: URI is trimmed to its mime prefix', () => {
  assert.equal(rowTitle({ kind: 'img', src: 'http://x/a.png' }),
    'http://x/a.png\n\nImage\nClick: open in editor · Double-click: quick crop');
  assert.equal(rowTitle({ kind: 'bg', src: 'http://x/b.png' }).split('\n')[2], 'Background image');
  // The huge base64 body never reaches the tooltip.
  const t = rowTitle({ kind: 'img', src: 'data:image/jpeg;base64,' + 'A'.repeat(5000) });
  assert.equal(t.split('\n')[0], 'data:image/jpeg;base64,…');
});

test('rowTitle for videos names the media URL and adapts the hint to a captured frame', () => {
  const withFrame = rowTitle({ kind: 'video', src: 'data:image/jpeg;base64,x', videoUrl: 'http://x/v.mp4' });
  assert.equal(withFrame.split('\n')[0], 'http://x/v.mp4');
  assert.match(withFrame, /crop frame/);
  const inPage = rowTitle({ kind: 'video', src: '', videoUrl: '' });
  assert.equal(inPage.split('\n')[0], '(in-page video)');
  assert.match(inPage, /Use the ⋯ menu/);
});

// ── thumbInitialSrc / dimText ──

test('thumbInitialSrc: src, then poster, then the play glyph for videos, else nothing', () => {
  assert.equal(thumbInitialSrc({ src: 'http://x/a.png' }, 'PLAY'), 'http://x/a.png');
  assert.equal(thumbInitialSrc({ src: '', posterUrl: 'http://x/p.jpg' }, 'PLAY'), 'http://x/p.jpg');
  assert.equal(thumbInitialSrc({ kind: 'video', src: '' }, 'PLAY'), 'PLAY');
  // A shared (server) row: no src at all — '' would point the <img> at the page document.
  assert.equal(thumbInitialSrc({ kind: 'img', src: '', shared: true }, 'PLAY'), '');
});

test('dimText renders only a fully-measured size', () => {
  assert.equal(dimText({ w: 120, h: 80 }), '120×80');
  assert.equal(dimText({ w: 0, h: 80 }), '');
});

// ── rowBadges ──

test('rowBadges: kind + format for a plain image; poster/meta tags where they apply', () => {
  const plain = rowBadges({ kind: 'img', src: 'http://x/a.png' });
  assert.deepEqual(plain.map((b) => [b.cls, b.text]),
    [['badge img', 'img'], ['badge fmt', 'png']]);

  const poster = rowBadges({ kind: 'img', src: 'http://x/p.jpg', poster: true });
  assert.equal(poster[1].cls, 'badge poster');
  assert.match(poster[1].title, /poster/);

  const meta = rowBadges({ kind: 'img', src: 'http://x/favicon.ico', meta: true });
  assert.equal(meta[1].cls, 'badge meta');
  assert.equal(meta[1].text, 'icon');

  // poster wins over meta (a poster row is never double-tagged).
  const both = rowBadges({ kind: 'img', src: 'http://x/p.jpg', poster: true, meta: true });
  assert.equal(both.filter((b) => b.cls === 'badge meta').length, 0);
});

test('rowBadges: a video shows its container format, falling back to "video" for blobs', () => {
  const mp4 = rowBadges({ kind: 'video', src: 'data:image/jpeg;base64,x', videoUrl: 'http://x/v.mp4' });
  assert.deepEqual(mp4.find((b) => b.cls === 'badge fmt').text, 'mp4');
  const blob = rowBadges({ kind: 'video', src: '', videoUrl: 'blob:xyz' });
  assert.equal(blob.find((b) => b.cls === 'badge fmt').text, 'video');
  // An undetectable image format shows the etc tag.
  const etc = rowBadges({ kind: 'img', src: 'blob:opaque' });
  assert.equal(etc.find((b) => b.cls === 'badge fmt').text, UNKNOWN_FORMAT);
});

test('rowBadges: the opened flag is last, icon-bearing, and only present when asked', () => {
  const b = rowBadges({ kind: 'img', src: 'http://x/a.png' }, { opened: true });
  const flag = b[b.length - 1];
  assert.equal(flag.cls, 'badge opened');
  assert.match(flag.html, /<svg/);
  assert.match(flag.html, / opened$/);
  assert.match(flag.title, /resume or add a copy/);
  assert.ok(!rowBadges({ kind: 'img', src: 'x' }).some((x) => x.cls === 'badge opened'));
});

// ── rowOutlineClass ──

test('rowOutlineClass: gold for server-stored, gray for local pins, gold wins', () => {
  assert.equal(rowOutlineClass({ shared: true }, {}), 'shared');
  // A LOCAL pin whose source is also stored on a server shows the golden cue too.
  const sharedSources = new Set(['http://x/a.png']);
  assert.equal(rowOutlineClass({ kind: 'img', src: 'http://x/a.png' }, { sharedSources, pinned: true }), 'shared');
  assert.equal(rowOutlineClass({ kind: 'img', src: 'http://x/b.png' }, { sharedSources, pinned: true }), 'pinned');
  assert.equal(rowOutlineClass({ kind: 'img', src: 'http://x/b.png' }, { sharedSources }), '');
});

test('rowOutlineClass: the server-pins filter OFF hides the golden cue (pin still shows)', () => {
  const sharedSources = new Set(['http://x/a.png']);
  assert.equal(rowOutlineClass({ shared: true }, { showServerPins: false }), '');
  assert.equal(rowOutlineClass({ kind: 'img', src: 'http://x/a.png' }, { showServerPins: false, sharedSources, pinned: true }), 'pinned');
  // An undefined toggle (state not yet loaded) counts as ON, like the old `!== false` test.
  assert.equal(rowOutlineClass({ shared: true }, { showServerPins: undefined }), 'shared');
});
