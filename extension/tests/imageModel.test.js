// Unit tests for src/lib/imageModel.js — the pure provenance/pin/search predicates extracted
// out of popup/popup.js (which is DOM/chrome-bound and untestable under node --test).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { sourceOf, posterImage, editableSrc, pinnable, sharedMatchesSearch } from '../src/lib/imageModel.js';

test('sourceOf: video → its media URL; image → its src; missing → empty', () => {
  assert.equal(sourceOf({ kind: 'video', videoUrl: 'https://a/v.mp4', src: 'x' }), 'https://a/v.mp4');
  assert.equal(sourceOf({ kind: 'img', src: 'https://a/i.png' }), 'https://a/i.png');
  assert.equal(sourceOf({ kind: 'video' }), '');   // no videoUrl
  assert.equal(sourceOf({ kind: 'img' }), '');      // no src
});

test('posterImage: builds an img row from a video poster with a derived name', () => {
  const p = posterImage({ posterUrl: 'https://a.com/pics/cover.jpg?v=2' });
  assert.equal(p.kind, 'img');
  assert.equal(p.src, 'https://a.com/pics/cover.jpg?v=2');
  assert.equal(p.poster, true);
  assert.equal(p.name, 'cover.jpg');   // filenameFromUrl strips the query
});

test('editableSrc: prefers the still, falls back to the poster', () => {
  assert.equal(editableSrc({ src: 'still.png', posterUrl: 'p.jpg' }), 'still.png');
  assert.equal(editableSrc({ posterUrl: 'p.jpg' }), 'p.jpg');
  assert.equal(editableSrc({}), '');
});

test('pinnable: true only with an openable source URL', () => {
  assert.equal(pinnable({ kind: 'img', src: 'https://a/i.png' }), true);
  assert.equal(pinnable({ kind: 'video', videoUrl: 'https://a/v.mp4' }), true);
  assert.equal(pinnable({ kind: 'img' }), false);
  assert.equal(pinnable({ kind: 'video' }), false);
});

test('sharedMatchesSearch: no query matches all; else matches name or source, case-insensitive', () => {
  const img = { name: 'Sunset.png', source: 'https://srv/project/42' };
  assert.equal(sharedMatchesSearch(img, ''), true);
  assert.equal(sharedMatchesSearch(img, 'sunset'), true);   // name, case-insensitive
  assert.equal(sharedMatchesSearch(img, 'srv/project'), true); // source
  assert.equal(sharedMatchesSearch(img, 'nope'), false);
  assert.equal(sharedMatchesSearch({}, 'x'), false);        // no name/source
});
