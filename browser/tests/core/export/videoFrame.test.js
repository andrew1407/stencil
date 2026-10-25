import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isVideoFile, isVideoUrl, videoFrameDataUrl } from '../../../js/core/export/videoFrame.js';

// The predicates that decide whether to capture a frame, and the capture's teardown over a
// stub <video>: a failed or finished capture lets go of its media.

test('isVideoFile: detects video by MIME type', () => {
  assert.equal(isVideoFile({ type: 'video/mp4', name: 'clip' }), true);
  assert.equal(isVideoFile({ type: 'video/webm', name: '' }), true);
  assert.equal(isVideoFile({ type: 'image/png', name: 'photo.png' }), false);
});

test('isVideoFile: falls back to the extension when MIME is missing', () => {
  assert.equal(isVideoFile({ type: '', name: 'movie.MP4' }), true);
  assert.equal(isVideoFile({ type: '', name: 'a.webm' }), true);
  assert.equal(isVideoFile({ type: '', name: 'b.mov' }), true);
  assert.equal(isVideoFile({ type: '', name: 'c.mkv' }), true);
  assert.equal(isVideoFile({ type: '', name: 'pic.jpg' }), false);
  assert.equal(isVideoFile({ type: '', name: 'no-ext' }), false);
});

test('isVideoFile: guards against null/undefined', () => {
  assert.equal(isVideoFile(null), false);
  assert.equal(isVideoFile(undefined), false);
});

test('isVideoUrl: detects a video by its path extension (open-image URL tab)', () => {
  assert.equal(isVideoUrl('https://ex.example/clip.mp4'), true);
  assert.equal(isVideoUrl('https://ex.example/a.webm'), true);
  assert.equal(isVideoUrl('https://ex.example/b.MOV'), true);
  assert.equal(isVideoUrl('https://ex.example/photo.jpg'), false);
  assert.equal(isVideoUrl('https://ex.example/no-ext'), false);
});

test('isVideoUrl: tolerates a trailing ?query / #hash after the extension', () => {
  assert.equal(isVideoUrl('https://ex.example/clip.mp4?token=abc'), true);
  assert.equal(isVideoUrl('https://ex.example/clip.mkv#t=10'), true);
  assert.equal(isVideoUrl('  https://ex.example/clip.m4v  '), true); // trimmed
  assert.equal(isVideoUrl('https://ex.example/mp4-in-path/pic.png'), false);
});

test('isVideoUrl: guards against non-strings', () => {
  assert.equal(isVideoUrl(null), false);
  assert.equal(isVideoUrl(undefined), false);
  assert.equal(isVideoUrl(42), false);
});

test('a capture that ends, failed or timed out, empties the video and revokes its URL', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const videos = [];
  const revoked = [];
  const saved = { document: globalThis.document, revoke: URL.revokeObjectURL };
  globalThis.document = { createElement: () => {
    const v = { on: {}, src: '', loads: 0, addEventListener(type, fn) { this.on[type] = fn; },
      removeAttribute(name) { if (name === 'src') this.src = ''; }, load() { this.loads++; } };
    videos.push(v);
    return v;
  } };
  URL.revokeObjectURL = (u) => revoked.push(u);
  try {
    const failed = videoFrameDataUrl('blob:a', 0);
    videos[0].on.error();
    await assert.rejects(failed, /failed to load/);
    const stalled = videoFrameDataUrl('https://ex.example/clip.mp4', 0);
    t.mock.timers.tick(8000);
    await assert.rejects(stalled, /timeout/);
  } finally {
    globalThis.document = saved.document;
    URL.revokeObjectURL = saved.revoke;
  }
  for (const v of videos) {
    assert.equal(v.src, '', 'no source left to keep fetching');
    assert.equal(v.loads, 1, 'load() resets the element, freeing its decoder');
  }
  assert.deepEqual(revoked, ['blob:a', 'https://ex.example/clip.mp4']);
});
