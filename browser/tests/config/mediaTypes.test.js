// ── mediaTypes.json drift guard ─────────────────────────────────────────────
// "What counts as an image or a video" used to live as a hand-kept literal in the browser,
// the extension, the desktop and the cli. The asset is now the one copy; this pins the
// BROWSER's consumption of it (the two video tests + the file-picker accept strings) and
// the invariants every other surface's wiring relies on.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import MEDIA from '../../js/config/mediaTypes.json' with { type: 'json' };
import { isVideoFile, isVideoUrl } from '../../js/core/export/videoFrame.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const read = (rel) => readFileSync(resolve(ROOT, rel), 'utf8');

test('shape: every extension list is lowercase, dot-free and duplicate-free', () => {
  const lists = [
    ['image.extensions', MEDIA.image.extensions],
    ['video.extensions', MEDIA.video.extensions],
    ...Object.entries(MEDIA.surfaces).flatMap(([s, sets]) =>
      Object.entries(sets).map(([k, v]) => [`surfaces.${s}.${k}`, v])),
  ];
  const ok = new RegExp(MEDIA.extensionPattern);
  for (const [name, list] of lists) {
    assert.ok(Array.isArray(list) && list.length, name);
    assert.equal(new Set(list).size, list.length, `${name} has a duplicate`);
    for (const ext of list) assert.match(ext, ok, `${name}: "${ext}"`);
  }
});

test('every surface set is a subset of the contract set it belongs to', () => {
  const video = new Set(MEDIA.video.extensions);
  const image = new Set(MEDIA.image.extensions);
  for (const [surface, sets] of Object.entries(MEDIA.surfaces))
    for (const [key, list] of Object.entries(sets)) {
      const want = key.toLowerCase().includes('video') ? video : image;
      for (const ext of list)
        assert.ok(want.has(ext), `surfaces.${surface}.${key}: "${ext}" is not in the contract set`);
    }
});

test('every surface set is documented in surfaceNotes', () => {
  const documented = Object.keys(MEDIA.surfaceNotes).sort();
  const declared = Object.entries(MEDIA.surfaces)
    .flatMap(([s, sets]) => Object.keys(sets).map((k) => `${s}.${k}`)).sort();
  assert.deepEqual(documented, declared);
});

test('the accept strings are the mime prefixes, and both pickers use the asset', () => {
  assert.equal(MEDIA.accept.image, `${MEDIA.image.mimePrefix}*`);
  assert.equal(MEDIA.accept.video, `${MEDIA.video.mimePrefix}*`);
  assert.equal(MEDIA.accept.imageOrVideo, `${MEDIA.accept.image},${MEDIA.accept.video}`);
  for (const rel of ['js/ui/openImage/sources/file.js', 'js/ui/chat/composer/chatComposer.js']) {
    const src = read(rel);
    assert.ok(src.includes('${MEDIA_TYPES.accept.imageOrVideo}'), `${rel} reads the asset`);
    assert.ok(!/accept="image\/\*/.test(src), `${rel} still hard-codes an accept list`);
  }
});

// The behavioural pin: the two detectors answer for exactly the extensions the asset
// lists for them, and for nothing else in the contract set.
test('isVideoFile / isVideoUrl match exactly their asset lists', () => {
  const { videoFile, videoUrl } = MEDIA.surfaces.browser;
  for (const ext of MEDIA.video.extensions) {
    assert.equal(isVideoFile({ name: `clip.${ext}` }), videoFile.includes(ext), `file .${ext}`);
    assert.equal(isVideoUrl(`https://h/clip.${ext}`), videoUrl.includes(ext), `url .${ext}`);
  }
  for (const ext of MEDIA.image.extensions) {
    assert.equal(isVideoFile({ name: `pic.${ext}` }), false, `file .${ext}`);
    assert.equal(isVideoUrl(`https://h/pic.${ext}`), false, `url .${ext}`);
  }
  // Case, query and fragment tolerance, unchanged by the move to the asset.
  assert.equal(isVideoFile({ name: 'CLIP.MP4' }), true);
  assert.equal(isVideoUrl('https://h/v.webm?token=1'), true);
  assert.equal(isVideoUrl('https://h/v.mov#t=3'), true);
  // MIME still wins over the name.
  assert.equal(isVideoFile({ name: 'no-extension', type: 'video/mp4' }), true);
});

test('normalisation rewrites are the ones every surface applies', () => {
  assert.deepEqual(MEDIA.normalize, { jpeg: 'jpg', 'svg+xml': 'svg', quicktime: 'mov' });
  // Substring, not whole-word: this is what the cli's norm() and the extension's
  // chained String.replace both do.
  const norm = (s) => Object.entries(MEDIA.normalize)
    .reduce((acc, [from, to]) => acc.split(from).join(to), s.toLowerCase());
  assert.equal(norm('x-JPEG'), 'x-jpg');
  assert.equal(norm('svg+xml'), 'svg');
  assert.equal(norm('quicktime'), 'mov');
});
