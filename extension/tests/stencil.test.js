import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildLaunchUrl, filenameFromUrl, guessMime, MAX_PAYLOAD } from '../src/lib/stencil.js';

test('buildLaunchUrl: round-trips the payload via the editor parse logic', () => {
  const payload = { dataUrl: 'data:image/png;base64,AAAA', name: 'p.png',
                    crop: { x: 1, y: 2, width: 3, height: 4 }, page: { size: 'A3' }, incognito: true };
  const url = buildLaunchUrl('http://localhost:8080/', payload);
  const marker = '#stencil=';
  const hash = '#' + url.split('#')[1];
  assert.ok(hash.startsWith(marker));
  const parsed = JSON.parse(decodeURIComponent(hash.slice(marker.length)));
  assert.deepEqual(parsed, payload);
});

test('buildLaunchUrl: strips any existing fragment first', () => {
  const url = buildLaunchUrl('http://localhost:8080/#stale', { dataUrl: 'x' });
  assert.ok(!url.includes('stale'));
  assert.ok(url.includes('#stencil='));
});

test('filenameFromUrl: paths, query strings, no-extension, data URIs', () => {
  assert.equal(filenameFromUrl('https://a.com/p/cat.jpg?x=1'), 'cat.jpg');
  assert.equal(filenameFromUrl('https://a.com/img'), 'img.png');
  assert.equal(filenameFromUrl('data:image/webp;base64,ZZ'), 'image.webp');
});

test('guessMime: known extensions and fallback', () => {
  assert.equal(guessMime('a.jpg'), 'image/jpeg');
  assert.equal(guessMime('a.svg'), 'image/svg+xml');
  assert.equal(guessMime('a.avif'), 'image/avif');
  assert.equal(guessMime('a.ico'), 'image/x-icon');
  assert.equal(guessMime('a.tiff'), 'image/tiff');
  assert.equal(guessMime('a.weird'), 'image/png');
});

test('MAX_PAYLOAD is a sane positive ceiling', () => {
  assert.ok(MAX_PAYLOAD > 100000);
});
