// Unit tests for the side-panel drag-to-pin URL extractor (src/lib/dragUrl.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { extractDraggedUrl, guessKindFromUrl } from '../src/lib/dragUrl.js';

const reader = (map) => (t) => map[t] || '';

test('uses text/uri-list (first non-comment line) when there is no text/html image', () => {
  const read = reader({ 'text/uri-list': '# c\nhttps://cdn/a.png\nhttps://x/b.png' });
  assert.equal(extractDraggedUrl(read), 'https://cdn/a.png');
});

test('prefers the <img> in text/html over a wrapping link in text/uri-list', () => {
  // A linked image (Wikipedia thumbnail): uri-list is the LINK, html is the real image.
  const read = reader({
    'text/uri-list': 'https://en.wikipedia.org/wiki/File:Cat.jpg',
    'text/html': '<a href="/wiki/File:Cat.jpg"><img src="https://upload/500px-Cat.jpg"></a>',
    'text/plain': 'https://en.wikipedia.org/wiki/File:Cat.jpg',
  });
  assert.equal(extractDraggedUrl(read), 'https://upload/500px-Cat.jpg');
});

test('matches <img>, <source>, and <video> src in text/html', () => {
  assert.equal(extractDraggedUrl(reader({ 'text/html': '<img src="https://c/x.jpg">' })), 'https://c/x.jpg');
  assert.equal(extractDraggedUrl(reader({ 'text/html': '<video><source src="https://c/v.mp4"></video>' })), 'https://c/v.mp4');
});

test('accepts an http(s) or data: URL from text/plain', () => {
  assert.equal(extractDraggedUrl(reader({ 'text/plain': 'https://c/z.gif' })), 'https://c/z.gif');
  assert.equal(extractDraggedUrl(reader({ 'text/plain': 'data:image/png;base64,AAAA' })), 'data:image/png;base64,AAAA');
  assert.equal(extractDraggedUrl(reader({ 'text/plain': 'not a url' })), '');
});

test('reads the URL line of text/x-moz-url ("URL\\ntitle")', () => {
  assert.equal(extractDraggedUrl(reader({ 'text/x-moz-url': 'https://c/m.png\nMy image' })), 'https://c/m.png');
});

test('returns "" when nothing usable is present', () => {
  assert.equal(extractDraggedUrl(reader({})), '');
});

test('guessKindFromUrl detects video containers, else image', () => {
  assert.equal(guessKindFromUrl('https://c/clip.mp4'), 'video');
  assert.equal(guessKindFromUrl('https://c/clip.webm?t=1'), 'video');
  assert.equal(guessKindFromUrl('https://c/photo.png'), 'image');
  assert.equal(guessKindFromUrl(''), 'image');
});
