import { test } from 'node:test';
import assert from 'node:assert/strict';
import { bgImageUrl, cssImageUrls, srcsetUrls, manifestIconUrls, nameFromUrl, videoHasFrame } from '../src/lib/image/pageImages.js';
import { mergeScanFrames, MAX_IMAGES, BLOCKED_SCHEMES } from '../src/lib/image/imageScan.js';

test('bgImageUrl: extracts url(...) in any quoting; rejects svg data URLs', () => {
  assert.equal(bgImageUrl('url("https://a.example/x.png")'), 'https://a.example/x.png');
  assert.equal(bgImageUrl("url('https://a.example/y.jpg')"), 'https://a.example/y.jpg');
  assert.equal(bgImageUrl('url(https://a.example/z.gif)'), 'https://a.example/z.gif');
  assert.equal(bgImageUrl('none'), '');
  // Inline-SVG data URIs ARE listed now (rasterize.js renders them for attach/hand-off).
  assert.equal(bgImageUrl('url(data:image/svg+xml;base64,AAAA)'), 'data:image/svg+xml;base64,AAAA');
  assert.equal(bgImageUrl(''), '');
});

test('cssImageUrls: every url() in a CSS value; drops svg-data + #fragment refs', () => {
  assert.deepEqual(cssImageUrls('url("https://a.example/x.png")'), ['https://a.example/x.png']);
  // Multiple backgrounds / image-set() nest several url() tokens — take them all, in order.
  assert.deepEqual(cssImageUrls('url(top.png), url(bottom.png)'), ['top.png', 'bottom.png']);
  assert.deepEqual(cssImageUrls('image-set(url("a.png") 1x, url("a@2x.png") 2x)'), ['a.png', 'a@2x.png']);
  assert.deepEqual(cssImageUrls('url(  spaced.png  )'), ['spaced.png']);
  // url(#…) is an in-document paint-server / filter / clip-path ref, NOT an image.
  assert.deepEqual(cssImageUrls('url(#clip)'), []);
  assert.deepEqual(cssImageUrls('url(data:image/svg+xml;base64,AAAA)'), ['data:image/svg+xml;base64,AAAA']);
  assert.deepEqual(cssImageUrls('none'), []);
  assert.deepEqual(cssImageUrls('linear-gradient(#000,#fff)'), []);
  assert.deepEqual(cssImageUrls(''), []);
});

test('srcsetUrls: candidate URLs, descriptors dropped', () => {
  assert.deepEqual(srcsetUrls('small.jpg 480w, large.jpg 1024w'), ['small.jpg', 'large.jpg']);
  assert.deepEqual(srcsetUrls('img.png 1x, img@2x.png 2x'), ['img.png', 'img@2x.png']);
  assert.deepEqual(srcsetUrls('solo.png'), ['solo.png']);
  assert.deepEqual(srcsetUrls('  x.png   2x  '), ['x.png']);
  assert.deepEqual(srcsetUrls(''), []);
});

test('manifestIconUrls: icon srcs resolved against the manifest URL', () => {
  const manifest = { icons: [{ src: 'icon-192.png' }, { src: '/abs/icon-512.png' }, { notSrc: 1 }] };
  assert.deepEqual(
    manifestIconUrls(manifest, 'https://a.example/app/site.webmanifest'),
    ['https://a.example/app/icon-192.png', 'https://a.example/abs/icon-512.png'],
  );
  assert.deepEqual(manifestIconUrls({}, 'https://a.example/m.json'), []);
  assert.deepEqual(manifestIconUrls(null, 'https://a.example/m.json'), []);
});

test('nameFromUrl: filename from path, query-stripped, data URL ext', () => {
  assert.equal(nameFromUrl('https://a.example/pics/cat.png?v=2'), 'cat.png');
  assert.equal(nameFromUrl('https://a.example/no-ext'), 'no-ext.png');
  assert.equal(nameFromUrl('data:image/jpeg;base64,AAAA'), 'image.jpeg');
  assert.equal(nameFromUrl('data:image/jpeg;base64,AAAA', 'video'), 'video.jpeg');
  assert.equal(nameFromUrl('not a url'), 'image.png');
});

test('videoHasFrame: needs decoded data, real dims, not poster-at-0', () => {
  assert.equal(videoHasFrame({ videoWidth: 640, videoHeight: 480, readyState: 2, paused: false, currentTime: 3 }), true);
  assert.equal(videoHasFrame({ videoWidth: 640, videoHeight: 480, readyState: 1, paused: false, currentTime: 3 }), false); // not enough data
  assert.equal(videoHasFrame({ videoWidth: 640, videoHeight: 480, readyState: 4, paused: true, currentTime: 0 }), false);  // poster showing
  assert.equal(videoHasFrame({ videoWidth: 0, videoHeight: 0, readyState: 4, paused: false, currentTime: 1 }), false);     // no dims
  assert.equal(videoHasFrame(null), false);
});

// ── mergeScanFrames (src/lib/imageScan.js): flatten all-frames scan results ──
test('mergeScanFrames dedupes by src across frames and keeps frame order', () => {
  const results = [
    { result: [{ src: 'a' }, { src: 'b' }] },
    null,                                        // frame the script could not run in
    { result: [{ src: 'b' }, { src: 'c' }] },    // dup across frames → first wins
  ];
  assert.deepEqual(mergeScanFrames(results).map((it) => it.src), ['a', 'b', 'c']);
  assert.deepEqual(mergeScanFrames([]), []);
  assert.deepEqual(mergeScanFrames(null), []);
});

test('mergeScanFrames caps at the limit; scan bounds match the popup values', () => {
  const results = [{ result: [{ src: 'a' }, { src: 'b' }, { src: 'c' }] }];
  assert.deepEqual(mergeScanFrames(results, 2).map((it) => it.src), ['a', 'b']);
  assert.equal(MAX_IMAGES, 1000);
  assert.ok(BLOCKED_SCHEMES.includes('chrome:') && BLOCKED_SCHEMES.includes('chrome-extension:'));
});
