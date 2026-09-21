import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildLaunchUrl, filenameFromUrl, guessMime, MAX_PAYLOAD, fetchAsDataUrl, isImageDataUrl } from '../../src/lib/stencil.js';

test('buildLaunchUrl: round-trips the payload via the editor parse logic', () => {
  const payload = { dataUrl: 'data:image/png;base64,AAAA', name: 'p.png',
                    crop: { x: 1, y: 2, width: 3, height: 4 }, page: { size: 'A3' }, incognito: true };
  const url = buildLaunchUrl('http://localhost:8080/', payload);
  const prefix = '#stencil=';
  const hash = '#' + url.split('#')[1];
  assert.ok(hash.startsWith(prefix));
  const parsed = JSON.parse(decodeURIComponent(hash.slice(prefix.length)));
  assert.deepEqual(parsed, payload);
});

test('buildLaunchUrl: strips any existing fragment first', () => {
  const url = buildLaunchUrl('http://localhost:8080/#stale', { dataUrl: 'x' });
  assert.ok(!url.includes('stale'));
  assert.ok(url.includes('#stencil='));
});

test('filenameFromUrl: paths, query strings, no-extension, data URIs', () => {
  assert.equal(filenameFromUrl('https://a.example/p/cat.jpg?x=1'), 'cat.jpg');
  assert.equal(filenameFromUrl('https://a.example/img'), 'img.png');
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

// The extension's host_permissions let fetch() reach ANY URL and bypass CORS, so a page-supplied
// URL must be scheme-gated: http(s)/blob and pass-through data: only.
test('fetchAsDataUrl: rejects non-http(s)/blob/data schemes', async () => {
  for (const bad of ['file:///etc/passwd', 'ftp://h/a.png', 'chrome://version', 'javascript:alert(1)']) {
    await assert.rejects(() => fetchAsDataUrl(bad), /unsupported URL scheme/);
  }
});

// The same permissions also reach the user's own network — page-harvested URLs
// naming private/internal hosts are refused before any fetch (urlGuard.js).
test('fetchAsDataUrl: rejects private/internal hosts', async () => {
  for (const bad of ['http://127.0.0.1/a.png', 'http://localhost:8080/a.png', 'http://10.0.0.5/a.png', 'http://169.254.169.254/latest/meta-data/', 'http://[::1]/a.png']) {
    await assert.rejects(() => fetchAsDataUrl(bad), /blocked private or internal address/);
  }
});

// The same-host carve-out: a trusted pageUrl lets an image on that SAME host through; cross-host
// private targets stay blocked, and the metadata IP is never allowed even from itself.
test('fetchAsDataUrl: pageUrl allows the scanned page\'s own private host only', async () => {
  const origFetch = globalThis.fetch;
  globalThis.fetch = async () => ({
    ok: true,
    headers: { get: () => 'image/png' },
    arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer,
  });
  try {
    assert.match(
      await fetchAsDataUrl('http://localhost:8080/a.png', { pageUrl: 'http://localhost:8080/page.html' }),
      /^data:image\/png;base64,/);
    assert.match(
      await fetchAsDataUrl('http://10.0.0.5/a.png', { pageUrl: 'http://10.0.0.5/index.html' }),
      /^data:image\/png;base64,/);
    await assert.rejects(
      () => fetchAsDataUrl('http://10.0.0.6/a.png', { pageUrl: 'http://10.0.0.5/index.html' }),
      /blocked private or internal address/);
    await assert.rejects(
      () => fetchAsDataUrl('http://127.0.0.1/a.png', { pageUrl: 'https://example.com/' }),
      /blocked private or internal address/);
    await assert.rejects(
      () => fetchAsDataUrl('http://169.254.169.254/latest/meta-data/', { pageUrl: 'http://169.254.169.254/' }),
      /blocked private or internal address/);
  } finally {
    globalThis.fetch = origFetch;
  }
});

test('fetchAsDataUrl: image data: URLs pass through unchanged (no fetch)', async () => {
  const d = 'data:image/png;base64,AAAA';
  assert.equal(await fetchAsDataUrl(d), d);
});

// A non-image data: URL is page-authored content — handlers taking a caller-supplied
// `dataUrl` instead of fetching one lean on this check.
test('isImageDataUrl: only data: URLs declaring an image type', () => {
  for (const ok of ['data:image/png;base64,AA', 'data:image/jpeg;base64,AA', 'data:image/svg+xml,<svg/>', 'DATA:IMAGE/GIF;base64,AA'])
    assert.equal(isImageDataUrl(ok), true, ok);
  for (const bad of [
    'data:text/html,<script>alert(1)</script>', 'data:application/json,{}', 'data:,hi',
    'data:imagex/png;base64,AA', 'https://cdn/a.png', 'javascript:alert(1)', '', null, undefined,
  ]) assert.equal(isImageDataUrl(bad), false, String(bad));
});

test('fetchAsDataUrl: a non-image data: URL is refused, not passed through', async () => {
  await assert.rejects(() => fetchAsDataUrl('data:text/html,<b>x</b>'), /not an image/);
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
