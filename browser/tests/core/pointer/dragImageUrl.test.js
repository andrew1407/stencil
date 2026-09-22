// Unit tests for the cross-page drag image-URL extractor (js/core/dragImageUrl.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { extractDraggedImageUrl, extractDraggedImageUrls, fetchFirstDraggedMediaFile, looksLikeImageUrl } from '../../../js/core/pointer/dragImageUrl.js';

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

// A dragged URL only works if it addresses the origin it came FROM: a protocol-relative src resolves against
// OUR scheme, which on an http:// dev server is a plain-http request to an https-only host.
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

// Galleries wrap every image in an <a>, so Chrome puts the LINK TARGET in text/uri-list and the image only in
// text/html's <img src>: preferring uri-list by position fetches the article page instead.
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

// GitHub serves an avatar with no extension and answers the repo's /raw/ path with a 302
// whose Access-Control-Allow-Origin is empty, so the first candidate cannot be the only try.
test('an extensionless <img src> still outranks the link it sits in', () => {
  const page = 'https://github.com/account';
  const img = 'https://avatars.githubusercontent.com/u/43030001?v=4';
  assert.deepEqual(
    extractDraggedImageUrls(reader({ 'text/uri-list': page, 'text/html': `<a href="${page}"><img src="${img}"></a>` })),
    [img, page], 'an <img> src is an image even when its url names no extension');
});

test('candidates are deduped and image-looking ones come first', () => {
  const same = 'https://cdn.test/a.png';
  assert.deepEqual(extractDraggedImageUrls(reader({ 'text/uri-list': same, 'text/html': `<img src="${same}">` })), [same]);
  assert.deepEqual(
    extractDraggedImageUrls(reader({ 'text/uri-list': 'https://x.test/page', 'text/html': '<img src="https://cdn.test/b.png">' })),
    ['https://cdn.test/b.png', 'https://x.test/page']);
});

test('the fetch falls through to the next candidate when the first is blocked', async () => {
  const png = new Blob([new Uint8Array([1, 2, 3])], { type: 'image/png' });
  const calls = [];
  globalThis.fetch = async (u) => {
    calls.push(u);
    if (u.startsWith('https://github.com/')) throw new TypeError('blocked by CORS');
    return { ok: true, blob: async () => png };
  };
  const file = await fetchFirstDraggedMediaFile(['https://github.com/account', 'https://cdn.test/a.png']);
  assert.equal(file.type, 'image/png');
  assert.deepEqual(calls, ['https://github.com/account', 'https://cdn.test/a.png']);
});

test('every candidate failing rejects with the FIRST failure, not the last', async () => {
  globalThis.fetch = async (u) =>
    u.endsWith('.png') ? { ok: false, status: 404 } : { ok: false, status: 500 };
  await assert.rejects(
    () => fetchFirstDraggedMediaFile(['https://x.test/a', 'https://x.test/b.png']),
    /HTTP 500/, 'the preferred candidate explains the drop best');
  await assert.rejects(() => fetchFirstDraggedMediaFile([]), /no image URL/);
});

// github.com/<user>/<repo>/blob/<path>.png ends in .png but serves an HTML page, so the
// name is only a hint — the fetch is what settles it.
test('a page url that merely ends in .png falls through to the real <img src>', async () => {
  const listed = 'https://github.com/andrew1407/stencil/blob/main/bot/assets/icon.png';
  const img = 'https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png';
  const urls = extractDraggedImageUrls(reader({ 'text/uri-list': listed, 'text/html': `<img src="${img}">` }));
  assert.deepEqual(urls, [listed, img], 'the .png-looking page is tried first');
  globalThis.fetch = async (u) => ({
    ok: true,
    blob: async () => new Blob(['x'], { type: u === listed ? 'text/html' : 'image/png' }),
  });
  const file = await fetchFirstDraggedMediaFile(urls);
  assert.equal(file.type, 'image/png', 'the html answer is refused and the next candidate wins');
});

// text/html is SERIALISED markup, so a query string arrives escaped; a literal '&amp;' in
// the url is a broken request (GitHub sizes its avatars '?s=64&amp;v=4').
test('an escaped <img src> is unescaped before it is used', () => {
  const [url] = extractDraggedImageUrls(
    reader({ 'text/html': '<img src="https://avatars.githubusercontent.com/u/43030001?s=64&amp;v=4">' }));
  assert.equal(url, 'https://avatars.githubusercontent.com/u/43030001?s=64&v=4');
  assert.equal(
    extractDraggedImageUrls(reader({ 'text/html': '<img src="https://x.test/a.png?w=1&amp;h=2&#39;">' }))[0],
    "https://x.test/a.png?w=1&h=2'");
});

test('text/x-moz-url ("URL\\ntitle") is a source too', () => {
  assert.deepEqual(extractDraggedImageUrls(reader({ 'text/x-moz-url': 'https://cdn.test/m.png\nMy image' })),
    ['https://cdn.test/m.png']);
  assert.deepEqual(extractDraggedImageUrls(reader({ 'text/x-moz-url': 'not a url\nt' })), []);
});

// github.com answers /raw/ and /blob/ with a 302 whose Access-Control-Allow-Origin is empty, so the
// browser refuses the redirect; raw.githubusercontent.com answers 200 with 'access-control-allow-origin: *'.
test('a github /raw/ url gains its raw.githubusercontent twin directly after it', () => {
  const raw = 'https://github.com/andrew1407/stencil/raw/main/browser/favicon.svg';
  const cdn = 'https://raw.githubusercontent.com/andrew1407/stencil/main/browser/favicon.svg';
  assert.deepEqual(extractDraggedImageUrls(reader({ 'text/uri-list': raw })), [raw, cdn],
    'the original is still tried first');
});

test('the twin keeps a multi-segment ref and a /blob/ path maps the same way', () => {
  const twin = (u) => extractDraggedImageUrls(reader({ 'text/uri-list': u }))[1];
  assert.equal(twin('https://github.com/andrew1407/stencil/raw/refs/heads/main/browser/favicon.svg'),
    'https://raw.githubusercontent.com/andrew1407/stencil/refs/heads/main/browser/favicon.svg');
  assert.equal(twin('https://github.com/andrew1407/stencil/blob/main/browser/favicon.svg?raw=true'),
    'https://raw.githubusercontent.com/andrew1407/stencil/main/browser/favicon.svg?raw=true');
});

test('an unrelated url gains nothing, and neither does a github page url', () => {
  for (const u of ['https://cdn.test/a.png', 'https://andrew1407.github.io/stencil/',
    'https://github.com/andrew1407/stencil/tree/main/bot/assets',
    'https://raw.githubusercontent.com/andrew1407/stencil/main/browser/favicon.svg'])
    assert.deepEqual(extractDraggedImageUrls(reader({ 'text/uri-list': u })), [u], u);
});

test('the walk reaches the twin when the original is blocked', async () => {
  const raw = 'https://github.com/andrew1407/stencil/raw/main/browser/favicon.svg';
  const cdn = 'https://raw.githubusercontent.com/andrew1407/stencil/main/browser/favicon.svg';
  const calls = [];
  globalThis.fetch = async (u) => {
    calls.push(u);
    if (u.startsWith('https://github.com/')) throw new TypeError('blocked by CORS');
    return { ok: true, blob: async () => new Blob(['<svg/>'], { type: 'image/svg+xml' }) };
  };
  const file = await fetchFirstDraggedMediaFile([raw]);
  assert.equal(file.type, 'image/svg+xml');
  assert.deepEqual(calls, [raw, cdn], 'the twin is walked even when the caller passed only the original');
});

test('every candidate failing names the count tried and the refused host', async () => {
  globalThis.fetch = async () => { throw new TypeError('blocked by CORS'); };
  await assert.rejects(() => fetchFirstDraggedMediaFile(['https://github.com/andrew1407/stencil/raw/main/a.svg']),
    /all 2 URLs in that drag failed; github\.com refused the request \(CORS or an unreachable host\)/);
  await assert.rejects(() => fetchFirstDraggedMediaFile(['https://cdn.test/a.png']),
    /the only URL in that drag failed; cdn\.test refused the request/);
  globalThis.fetch = async () => ({ ok: false, status: 403 });
  await assert.rejects(() => fetchFirstDraggedMediaFile(['https://cdn.test/a.png']), /cdn\.test answered HTTP 403/);
});
