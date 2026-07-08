import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isVideoFile, isVideoUrl } from '../js/core/videoFrame.js';

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

test('isVideoUrl: detects a video by its path extension (open-image URL tab)', () => {
  assert.equal(isVideoUrl('https://ex.com/clip.mp4'), true);
  assert.equal(isVideoUrl('https://ex.com/a.webm'), true);
  assert.equal(isVideoUrl('https://ex.com/b.MOV'), true);
  assert.equal(isVideoUrl('https://ex.com/photo.jpg'), false);
  assert.equal(isVideoUrl('https://ex.com/no-ext'), false);
});

test('isVideoUrl: tolerates a trailing ?query / #hash after the extension', () => {
  assert.equal(isVideoUrl('https://ex.com/clip.mp4?token=abc'), true);
  assert.equal(isVideoUrl('https://ex.com/clip.mkv#t=10'), true);
  assert.equal(isVideoUrl('  https://ex.com/clip.m4v  '), true); // trimmed
  assert.equal(isVideoUrl('https://ex.com/mp4-in-path/pic.png'), false);
});

test('isVideoUrl: guards against non-strings', () => {
  assert.equal(isVideoUrl(null), false);
  assert.equal(isVideoUrl(undefined), false);
  assert.equal(isVideoUrl(42), false);
});
