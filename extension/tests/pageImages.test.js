import { test } from 'node:test';
import assert from 'node:assert/strict';
import { bgImageUrl, nameFromUrl, videoHasFrame } from '../src/lib/pageImages.js';

test('bgImageUrl: extracts url(...) in any quoting; rejects svg data URLs', () => {
  assert.equal(bgImageUrl('url("https://a.com/x.png")'), 'https://a.com/x.png');
  assert.equal(bgImageUrl("url('https://a.com/y.jpg')"), 'https://a.com/y.jpg');
  assert.equal(bgImageUrl('url(https://a.com/z.gif)'), 'https://a.com/z.gif');
  assert.equal(bgImageUrl('none'), '');
  assert.equal(bgImageUrl('url(data:image/svg+xml;base64,AAAA)'), '');   // inline svg → not shareable
  assert.equal(bgImageUrl(''), '');
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
