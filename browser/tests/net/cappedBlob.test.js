// js/net/cappedBody.js readBlobCapped: a server original, a launch or a fetched image source is a
// Blob of its own type, read only up to the 64 MiB cap (above the server's 32 MiB upload limit),
// and ServerConnection.fetchFile reads the file through it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MAX_FETCH_BYTES, readBlobCapped } from '../../js/net/cappedBody.js';
import { ServerConnection } from '../../js/net/serverConnection.js';

const endless = () => new ReadableStream({ pull(c) { c.enqueue(new Uint8Array(1024)); } });

test('a body within the cap comes back as a Blob of its content type', async () => {
  const blob = await readBlobCapped(new Response(new Uint8Array([1, 2, 3]), { headers: { 'content-type': 'image/png' } }));
  assert.equal(blob.type, 'image/png');
  assert.deepEqual([...new Uint8Array(await blob.arrayBuffer())], [1, 2, 3]);
});

test('a declared or an endless body past the cap is refused', async () => {
  const declared = new Response('x', { headers: { 'content-length': String(MAX_FETCH_BYTES + 1) } });
  await assert.rejects(() => readBlobCapped(declared), /67108864-byte fetch cap/);
  await assert.rejects(() => readBlobCapped(new Response(endless()), 4096), /4096-byte fetch cap/);
});

test('a plain object reads through its own blob() and is still measured', async () => {
  const small = new Blob([new Uint8Array(4)], { type: 'image/jpeg' });
  assert.equal(await readBlobCapped({ blob: async () => small }), small);
  await assert.rejects(() => readBlobCapped({ blob: async () => small }, 3), /3-byte fetch cap/);
});

test('fetchFile stops an endless original at the cap', async () => {
  const fetchImpl = async (url) => (/\/files\//.test(String(url))
    ? new Response(endless(), { status: 200, headers: { 'content-type': 'image/png' } })
    : new Response('{}', { status: 200, headers: { 'content-type': 'application/json' } }));
  const conn = new ServerConnection('http://srv:9', { token: 't', fetchImpl });
  await assert.rejects(() => conn.fetchFile('r1', 'original'), /fetch cap/);
});
