// Unit tests for the cross-page drag image-URL extractor (js/core/dragImageUrl.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { extractDraggedImageUrl } from '../js/core/dragImageUrl.js';

// Build a read(type) backed by a { type: value } map (missing types return '').
const reader = (map) => (t) => map[t] || '';

test('prefers text/uri-list, skipping comment lines', () => {
  const read = reader({ 'text/uri-list': '# comment\nhttps://cdn.example.com/a.png\nhttps://x/b.png' });
  assert.equal(extractDraggedImageUrl(read), 'https://cdn.example.com/a.png');
});

test('falls back to the first <img src> in text/html', () => {
  const read = reader({ 'text/html': '<div><img alt="x" src="https://cdn/x.jpg" width="2"></div>' });
  assert.equal(extractDraggedImageUrl(read), 'https://cdn/x.jpg');
});

test('html match handles single quotes + extra attributes', () => {
  const read = reader({ 'text/html': "<img data-a='1' src='https://cdn/y.webp' >" });
  assert.equal(extractDraggedImageUrl(read), 'https://cdn/y.webp');
});

test('falls back to text/plain only when it is an http(s) URL', () => {
  assert.equal(extractDraggedImageUrl(reader({ 'text/plain': 'https://cdn/z.gif' })), 'https://cdn/z.gif');
  assert.equal(extractDraggedImageUrl(reader({ 'text/plain': 'just some text' })), '');
});

test('uri-list wins over html and plain', () => {
  const read = reader({
    'text/uri-list': 'https://win/a.png',
    'text/html': '<img src="https://lose/b.png">',
    'text/plain': 'https://lose/c.png',
  });
  assert.equal(extractDraggedImageUrl(read), 'https://win/a.png');
});

test('returns "" when nothing image-like is present', () => {
  assert.equal(extractDraggedImageUrl(reader({})), '');
  assert.equal(extractDraggedImageUrl(reader({ 'text/plain': 'hello' })), '');
});

test('tolerates a read() that throws for a type', () => {
  const read = (t) => { if (t === 'text/uri-list') throw new Error('unavailable'); if (t === 'text/plain') return 'https://cdn/ok.png'; return ''; };
  assert.equal(extractDraggedImageUrl(read), 'https://cdn/ok.png');
});
