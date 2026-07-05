import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildLaunchUrl, filenameFromUrl, guessMime, MAX_PAYLOAD, fetchAsDataUrl } from '../src/lib/stencil.js';

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

// The extension's host_permissions let fetch() reach ANY URL and bypass CORS, so a
// page-supplied URL must be scheme-gated: only http(s)/blob (and pass-through data:)
// are fetched — never file:/chrome:/ftp:/javascript:.
test('fetchAsDataUrl: rejects non-http(s)/blob/data schemes', async () => {
  for (const bad of ['file:///etc/passwd', 'ftp://h/a.png', 'chrome://version', 'javascript:alert(1)']) {
    await assert.rejects(() => fetchAsDataUrl(bad), /unsupported URL scheme/);
  }
});

test('fetchAsDataUrl: data: URLs pass through unchanged (no fetch)', async () => {
  const d = 'data:image/png;base64,AAAA';
  assert.equal(await fetchAsDataUrl(d), d);
});

test('fetchAsDataUrl: allows http(s)/blob and returns a data URL', async () => {
  const origFetch = globalThis.fetch;
  globalThis.fetch = async () => ({
    ok: true,
    headers: { get: () => 'image/png' },
    arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer,
  });
  try {
    assert.match(await fetchAsDataUrl('https://x.example/a.png'), /^data:image\/png;base64,/);
    assert.match(await fetchAsDataUrl('blob:https://x.example/uuid'), /^data:image\/png;base64,/);
  } finally {
    globalThis.fetch = origFetch;
  }
});
