// guardedFetch + readCapped (src/lib/connection/urlGuard.js): the one road every page-derived
// byte read takes. A redirect is refused, never followed — the first hop is the only one the
// guard can see — and no body is buffered past the cap, whether or not it declares its size.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import {
  guardedFetch, readCapped, readBlobCapped, MAX_FETCH_BYTES, BLOCKED_ADDRESS, REDIRECT_REFUSED,
} from '../../../src/lib/connection/urlGuard.js';
import { fetchAsDataUrl } from '../../../src/lib/stencil.js';
import { decodeSize } from '../../../src/lib/image/rasterize.js';

const withFetch = async (impl, fn) => {
  const orig = globalThis.fetch;
  globalThis.fetch = impl;
  try { return await fn(); } finally { globalThis.fetch = orig; }
};
const headers = (h = {}) => ({ get: (k) => h[k.toLowerCase()] ?? null });
const streamOf = (chunks, onCancel = () => {}) => new ReadableStream({
  pull(c) { if (chunks.length) c.enqueue(chunks.shift()); else c.close(); },
  cancel: onCancel,
});

test('64 MiB, the cap the CLI and pystencil read up to', () => {
  assert.equal(MAX_FETCH_BYTES, 64 * 1024 * 1024);
});

test('a blocked host is refused before any request goes out', async () => {
  let calls = 0;
  await withFetch(async () => { calls++; return { ok: true }; }, async () => {
    for (const url of ['http://10.0.0.5/x.png', 'http://[64:ff9b::a9fe:a9fe]/', 'file:///etc/passwd'])
      await assert.rejects(() => guardedFetch(url), new RegExp(BLOCKED_ADDRESS), url);
  });
  assert.equal(calls, 0);
});

test('every request asks for a manual redirect, and the caller init still rides along', async () => {
  const seen = [];
  await withFetch(async (url, init) => { seen.push(init); return { ok: true, status: 200, type: 'basic' }; },
    () => guardedFetch('https://cdn.example/a.png', {}, { headers: { Accept: 'image/*' }, redirect: 'follow' }));
  assert.equal(seen[0].redirect, 'manual', 'a caller cannot switch following back on');
  assert.deepEqual(seen[0].headers, { Accept: 'image/*' });
});

// Chrome hides a manual redirect's target, so the answer is an opaque redirect: refused outright.
test('an opaque redirect, or any visible 30x, is refused and its body released', async () => {
  for (const resp of [{ type: 'opaqueredirect', status: 0 }, ...[301, 302, 303, 307, 308].map((status) => (
    { type: 'basic', status, headers: headers({ location: 'http://169.254.169.254/' }) }))]) {
    let cancelled = false;
    let calls = 0;
    const withBody = { ...resp, body: { cancel: async () => { cancelled = true; } } };
    await withFetch(async () => { calls++; return withBody; }, () =>
      assert.rejects(() => guardedFetch('https://cdn.example/a.png'), new RegExp(REDIRECT_REFUSED)));
    assert.equal(calls, 1, 'the Location is never requested');
    assert.ok(cancelled, `status ${resp.status}: the body is released`);
  }
});

// An http→https upgrade is the commonest redirect of all; its twin is fetched, never the Location.
test('a redirected http URL is asked for once as its https twin, which may not redirect again', async () => {
  const asked = [];
  const ok = await withFetch(async (url) => {
    asked.push(url);
    return url.startsWith('http:') ? { type: 'opaqueredirect', status: 0 } : { type: 'basic', status: 200, ok: true };
  }, () => guardedFetch('http://cdn.example/a.png?x=1'));
  assert.equal(ok.status, 200);
  assert.deepEqual(asked, ['http://cdn.example/a.png?x=1', 'https://cdn.example/a.png?x=1']);
  const again = [];
  await withFetch(async (url) => { again.push(url); return { type: 'opaqueredirect', status: 0 }; }, () =>
    assert.rejects(() => guardedFetch('http://cdn.example/a.png'), new RegExp(REDIRECT_REFUSED)));
  assert.equal(again.length, 2, 'the twin is asked once, never a third hop');
});

test('any other status reaches the caller, which reads resp.ok itself', async () => {
  for (const status of [200, 204, 304, 404, 500]) {
    const resp = await withFetch(async () => ({ status, ok: status < 300, type: 'basic' }),
      () => guardedFetch('https://cdn.example/a.png'));
    assert.equal(resp.status, status);
  }
});

// Real fetch semantics over loopback: the first hop bounces to a second server standing in for
// an internal host. Following it would register a hit there.
test('a live 302 stops at the first hop: the redirect target is never requested', async () => {
  const hits = { outer: 0, inner: 0 };
  const inner = createServer((req, res) => { hits.inner++; res.end('secret'); });
  await new Promise((r) => inner.listen(0, '127.0.0.1', r));
  const target = `http://127.0.0.1:${inner.address().port}/latest/meta-data/`;
  const outer = createServer((req, res) => { hits.outer++; res.writeHead(302, { Location: target }).end(); });
  await new Promise((r) => outer.listen(0, '127.0.0.1', r));
  try {
    const url = `http://127.0.0.1:${outer.address().port}/img.png`;
    // The https twin finds no TLS on that port, so the whole fetch fails — without the Location.
    await assert.rejects(() => guardedFetch(url, { allowLoopback: true }));
    assert.deepEqual(hits, { outer: 1, inner: 0 });
  } finally {
    outer.close();
    inner.close();
  }
});

test('readCapped refuses a declared oversize body without reading it', async () => {
  let cancelled = false;
  let read = false;
  const resp = {
    headers: headers({ 'content-length': String(1001) }),
    body: { cancel: async () => { cancelled = true; }, getReader: () => { read = true; } },
  };
  await assert.rejects(() => readCapped(resp, 1000), /exceeds the 1000-byte fetch cap/);
  assert.ok(cancelled);
  assert.equal(read, false);
});

test('readCapped stops a streamed body the moment it passes the cap, size undeclared', async () => {
  let cancelled = false;
  const chunks = [new Uint8Array(600), new Uint8Array(600), new Uint8Array(600)];
  const resp = { headers: headers(), body: streamOf(chunks, () => { cancelled = true; }) };
  await assert.rejects(() => readCapped(resp, 1000), /fetch cap/);
  assert.ok(cancelled, 'the rest of the stream is cancelled');
  assert.equal(chunks.length, 1, 'the third chunk is never pulled');
});

test('readCapped returns the exact bytes of a body within the cap', async () => {
  const resp = { headers: headers(), body: streamOf([Uint8Array.of(1, 2), Uint8Array.of(3)]) };
  assert.deepEqual([...new Uint8Array(await readCapped(resp, 3))], [1, 2, 3]);
  const plain = { headers: headers(), arrayBuffer: async () => Uint8Array.of(9, 9).buffer };
  assert.deepEqual([...new Uint8Array(await readCapped(plain, 2))], [9, 9]);
  await assert.rejects(() => readCapped(plain, 1), /fetch cap/, 'no stream: measured after the read');
});

test('readBlobCapped keeps the response type', async () => {
  const resp = { headers: headers({ 'content-type': 'video/mp4' }), body: streamOf([Uint8Array.of(7)]) };
  const blob = await readBlobCapped(resp);
  assert.equal(blob.type, 'video/mp4');
  assert.equal(blob.size, 1);
});

test('fetchAsDataUrl refuses a redirected image and an oversize one', async () => {
  await withFetch(async () => ({ type: 'opaqueredirect', status: 0, ok: false }), () =>
    assert.rejects(() => fetchAsDataUrl('https://cdn.example/a.png'), new RegExp(REDIRECT_REFUSED)));
  const big = String(MAX_FETCH_BYTES + 1);
  await withFetch(async () => ({
    ok: true, status: 200, type: 'basic',
    headers: headers({ 'content-type': 'image/png', 'content-length': big }),
    arrayBuffer: async () => { throw new Error('the body was read'); },
  }), () => assert.rejects(() => fetchAsDataUrl('https://cdn.example/a.png'), /fetch cap/));
});

// An <img src> follows redirects on its own, so an http(s) source never reaches one: its bytes
// come through the guarded fetch and the element decodes them from a blob: URL.
test('rasterize fetches an http(s) source once under the guard, then decodes the bytes', async () => {
  const fetched = [];
  const srcs = [];
  const deps = {
    createBitmap: null,
    toBlob: async (url) => { fetched.push(url); return new Blob(['<svg/>'], { type: 'image/svg+xml' }); },
    objectUrl: () => 'blob:stub', revokeUrl: () => {}, timer: () => 0, clearTimer: () => {},
    makeImage: () => {
      const on = {};
      return {
        naturalWidth: 40, naturalHeight: 20, addEventListener: (t, fn) => { on[t] = fn; },
        set src(v) { srcs.push(v); queueMicrotask(() => on.load()); },
      };
    },
  };
  assert.deepEqual(await decodeSize({ dataUrl: 'https://cdn.example/logo.svg' }, { deps }), { width: 40, height: 20 });
  assert.deepEqual(fetched, ['https://cdn.example/logo.svg']);
  assert.deepEqual(srcs, ['blob:stub']);
});
