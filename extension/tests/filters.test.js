import { test } from 'node:test';
import assert from 'node:assert/strict';
import { formatOf, distinctFormats, extractCssUrls, passesFilters, UNKNOWN_FORMAT } from '../src/lib/filters.js';

test('formatOf: extensions, query strings, data URIs, normalisation', () => {
  assert.equal(formatOf('https://a.com/x/cat.PNG'), 'png');
  assert.equal(formatOf('https://a.com/x/cat.jpg?v=2'), 'jpg');
  assert.equal(formatOf('https://a.com/x/cat.jpeg'), 'jpg');
  assert.equal(formatOf('data:image/webp;base64,ZZ'), 'webp');
  assert.equal(formatOf('data:image/svg+xml,<svg/>'), 'svg');
  assert.equal(formatOf('https://a.com/no-extension'), '');
});

test('distinctFormats: unique + sorted', () => {
  const items = [{ src: 'a.png' }, { src: 'b.jpg' }, { src: 'c.PNG' }, { src: 'd' }];
  assert.deepEqual(distinctFormats(items), ['jpg', 'png']);
});

test('extractCssUrls: single, multiple, quotes, none', () => {
  assert.deepEqual(extractCssUrls('url("a.png")'), ['a.png']);
  assert.deepEqual(extractCssUrls("url(a.png), url('b.jpg')"), ['a.png', 'b.jpg']);
  assert.deepEqual(extractCssUrls('none'), []);
});

test('passesFilters: include img / bg toggles', () => {
  const img = { kind: 'img', src: 'a.png', name: 'a.png', w: 100, h: 100 };
  const bg = { kind: 'bg', src: 'b.png', name: 'b.png', w: 100, h: 100 };
  assert.equal(passesFilters(img, { includeImg: false }), false);
  assert.equal(passesFilters(bg, { includeBg: false }), false);
  assert.equal(passesFilters(img, { includeImg: true, includeBg: false }), true);
});

test('passesFilters: video toggle + videos bypass the format set', () => {
  const vid = { kind: 'video', src: 'data:image/jpeg;base64,ZZ', name: 'clip.mp4', videoUrl: 'https://x.com/clip.mp4', w: 1280, h: 720 };
  // The dedicated 'video' toggle hides/shows videos.
  assert.equal(passesFilters(vid, { includeVideo: false }), false);
  assert.equal(passesFilters(vid, { includeVideo: true }), true);
  // A video is NOT removed by the image-format checkboxes (its still is a jpg frame).
  assert.equal(passesFilters(vid, { formats: ['png'] }), true);
  assert.equal(passesFilters(vid, { formats: [] }), true);
  // But an <img> still is.
  const jpg = { kind: 'img', src: 'b.jpg', name: 'b.jpg', w: 10, h: 10 };
  assert.equal(passesFilters(jpg, { formats: ['png'] }), false);
});

test('passesFilters: search matches a video media URL too', () => {
  const vid = { kind: 'video', src: 'data:image/jpeg;base64,ZZ', name: 'video.jpeg', videoUrl: 'https://cdn.example.com/reel-42.mp4', w: 0, h: 0 };
  assert.equal(passesFilters(vid, { search: 'reel-42' }), true);
  assert.equal(passesFilters(vid, { search: 'cdn.example' }), true);
  assert.equal(passesFilters(vid, { search: 'nope' }), false);
});

test('passesFilters: search matches name or URL', () => {
  const it = { kind: 'img', src: 'https://x.com/hero-banner.png', name: 'hero-banner.png', w: 10, h: 10 };
  assert.equal(passesFilters(it, { search: 'banner' }), true);
  assert.equal(passesFilters(it, { search: 'x.com' }), true);
  assert.equal(passesFilters(it, { search: 'nope' }), false);
});

test('passesFilters: format checkbox set', () => {
  const png = { kind: 'img', src: 'a.png', name: 'a.png', w: 10, h: 10 };
  const jpg = { kind: 'img', src: 'b.jpg', name: 'b.jpg', w: 10, h: 10 };
  const unknown = { kind: 'img', src: 'x', name: 'x', w: 1, h: 1 };
  assert.equal(passesFilters(png, { formats: ['png', 'gif'] }), true);
  assert.equal(passesFilters(jpg, { formats: ['png', 'gif'] }), false);
  // undetectable format is bucketed as 'etc' — passes only when 'etc' is in the set
  assert.equal(UNKNOWN_FORMAT, 'etc');
  assert.equal(passesFilters(unknown, { formats: ['png'] }), false);
  assert.equal(passesFilters(unknown, { formats: ['png', UNKNOWN_FORMAT] }), true);
  // no formats key → no format filtering
  assert.equal(passesFilters(jpg, {}), true);
  assert.equal(passesFilters(unknown, {}), true);
  // empty set → nothing passes (including undetectable)
  assert.equal(passesFilters(jpg, { formats: [] }), false);
  assert.equal(passesFilters(unknown, { formats: [] }), false);
});

test('passesFilters: min/max dims apply when known', () => {
  const it = { kind: 'img', src: 'a.png', name: 'a.png', w: 200, h: 150 };
  assert.equal(passesFilters(it, { minW: 100, maxW: 300 }), true);
  assert.equal(passesFilters(it, { minW: 250 }), false);
  assert.equal(passesFilters(it, { maxH: 100 }), false);
});

test('passesFilters: unknown dims pass the size filters', () => {
  const bg = { kind: 'bg', src: 'b.png', name: 'b.png', w: 0, h: 0 };
  assert.equal(passesFilters(bg, { minW: 500, maxH: 10 }), true);
});

test('passesFilters: empty bounds (null) impose no limit', () => {
  const it = { kind: 'img', src: 'a.png', name: 'a.png', w: 5, h: 5 };
  assert.equal(passesFilters(it, { minW: null, maxW: null, minH: null, maxH: null }), true);
});
