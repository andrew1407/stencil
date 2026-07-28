import { test } from 'node:test';
import assert from 'node:assert/strict';
import { bgImageUrl, cssImageUrls, srcsetUrls, manifestIconUrls, nameFromUrl, videoHasFrame } from '../src/lib/pageImages.js';

test('bgImageUrl: extracts url(...) in any quoting; rejects svg data URLs', () => {
  assert.equal(bgImageUrl('url("https://a.com/x.png")'), 'https://a.com/x.png');
  assert.equal(bgImageUrl("url('https://a.com/y.jpg')"), 'https://a.com/y.jpg');
  assert.equal(bgImageUrl('url(https://a.com/z.gif)'), 'https://a.com/z.gif');
  assert.equal(bgImageUrl('none'), '');
  assert.equal(bgImageUrl('url(data:image/svg+xml;base64,AAAA)'), '');   // inline svg → not shareable
  assert.equal(bgImageUrl(''), '');
});

test('cssImageUrls: every url() in a CSS value; drops svg-data + #fragment refs', () => {
  assert.deepEqual(cssImageUrls('url("https://a.com/x.png")'), ['https://a.com/x.png']);
  // Multiple backgrounds / image-set() nest several url() tokens — take them all, in order.
  assert.deepEqual(cssImageUrls('url(top.png), url(bottom.png)'), ['top.png', 'bottom.png']);
  assert.deepEqual(cssImageUrls('image-set(url("a.png") 1x, url("a@2x.png") 2x)'), ['a.png', 'a@2x.png']);
  assert.deepEqual(cssImageUrls('url(  spaced.png  )'), ['spaced.png']);
  // url(#…) is an in-document paint-server / filter / clip-path ref, NOT an image.
  assert.deepEqual(cssImageUrls('url(#clip)'), []);
  assert.deepEqual(cssImageUrls('url(data:image/svg+xml;base64,AAAA)'), []);   // inline svg
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
    manifestIconUrls(manifest, 'https://a.com/app/site.webmanifest'),
    ['https://a.com/app/icon-192.png', 'https://a.com/abs/icon-512.png'],
  );
  assert.deepEqual(manifestIconUrls({}, 'https://a.com/m.json'), []);
  assert.deepEqual(manifestIconUrls(null, 'https://a.com/m.json'), []);
});

test('nameFromUrl: filename from path, query-stripped, data URL ext', () => {
  assert.equal(nameFromUrl('https://a.com/pics/cat.png?v=2'), 'cat.png');
  assert.equal(nameFromUrl('https://a.com/no-ext'), 'no-ext.png');
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
