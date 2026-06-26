import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isVideoFile } from '../js/core/videoFrame.js';

// videoFrameDataUrl / videoFileToImageFile need a DOM <video> + canvas, so only the
// pure isVideoFile predicate (which decides whether to capture a frame) is unit-tested.

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
