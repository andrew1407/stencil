// Unit tests for the cross-page drag image-URL extractor (js/core/dragImageUrl.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { extractDraggedImageUrl, looksLikeImageUrl } from '../js/core/dragImageUrl.js';

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

// ── Relative and protocol-relative sources ──────────────────────────────────
// A dragged URL only works if it addresses the origin it came FROM. Real pages
// still emit protocol-relative srcs, and a bare fetch of one
// resolves against OUR scheme — on an http:// dev server that is a plain-http
// request to an https-only host, which fails and used to be reported as the remote
// site blocking cross-origin downloads.
test('protocol-relative sources are pinned to https, not to our scheme', () => {
  const url = '//raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png';
  assert.equal(extractDraggedImageUrl(reader({ 'text/uri-list': url })), `https:${url}`);
  assert.equal(extractDraggedImageUrl(reader({ 'text/html': `<img src="${url}">` })), `https:${url}`);
});

test('root-relative sources are refused — they carry no origin to fetch from', () => {
  // Left alone these resolve against the Stencil app itself and 404 there, which
  // reads as "the remote site refused us" when nothing of the sort happened.
  assert.equal(extractDraggedImageUrl(reader({ 'text/html': '<img src="/raw/main/icon.png">' })), '');
  assert.equal(extractDraggedImageUrl(reader({ 'text/uri-list': '/raw/main/icon.png' })), '');
  assert.equal(extractDraggedImageUrl(reader({ 'text/html': '<img src="thumb/icon.png">' })), '');
});

test('an unusable html src still lets text/plain win', () => {
  // The html payload is relative (unusable), but the drag also carried a real URL.
  assert.equal(
    extractDraggedImageUrl(reader({ 'text/html': '<img src="/rel.jpg">', 'text/plain': 'https://x.test/a.png' })),
    'https://x.test/a.png');
});

test('data: and blob: sources pass through untouched', () => {
  const data = 'data:image/png;base64,iVBORw0KGgo=';
  assert.equal(extractDraggedImageUrl(reader({ 'text/uri-list': data })), data);
  assert.equal(extractDraggedImageUrl(reader({ 'text/html': '<img src="blob:https://x.test/abc">' })), 'blob:https://x.test/abc');
});

// ── A LINKED image: the payload that actually broke gallery drags ──────────
// Galleries and file browsers wrap every image in an <a> to its own page. Chrome
// then puts the LINK TARGET in text/uri-list and the real image only
// in text/html's <img src>. Preferring uri-list by position fetched the article
// page — HTML, from a host that sends no CORS headers — so the drop failed and
// reported the site as blocking cross-origin downloads.
test('a linked image picks the <img src>, not the link target', () => {
  const page = 'https://github.com/andrew1407/stencil/tree/main/bot/assets';
  const img = 'https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png';
  assert.equal(
    extractDraggedImageUrl(reader({ 'text/uri-list': page, 'text/html': `<a href="${page}"><img src="${img}"></a>`, 'text/plain': page })),
    img);
});

test('uri-list still wins when it is the one naming an image', () => {
  const listed = 'https://cdn.test/real.png';
  const inHtml = 'https://cdn.test/thumb.jpg';
  assert.equal(
    extractDraggedImageUrl(reader({ 'text/uri-list': listed, 'text/html': `<img src="${inHtml}">` })),
    listed, 'source order is the tiebreak when both look like images');
});

test('with nothing image-shaped anywhere, source order still decides', () => {
  // A query-string CDN URL names no extension; guessing wrong here must not stop
  // the drop — the fetch decides, and its content-type check is the real gate.
  const listed = 'https://cdn.test/render?id=9';
  assert.equal(extractDraggedImageUrl(reader({ 'text/uri-list': listed, 'text/plain': 'https://x.test/p' })), listed);
});

test('looksLikeImageUrl: extensions, query strings, data and blob', () => {
  assert.ok(looksLikeImageUrl('https://x.test/a.JPG?utm=1'));
  assert.ok(looksLikeImageUrl('https://x.test/a.webp#frag'));
  assert.ok(looksLikeImageUrl('data:image/png;base64,AAA'));
  assert.ok(looksLikeImageUrl('blob:https://x.test/abc'));
  assert.ok(!looksLikeImageUrl('https://github.com/andrew1407/stencil/tree/main/bot/assets'));
  assert.ok(!looksLikeImageUrl('https://x.test/a.jpg.html'));
});
