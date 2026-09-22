// js/net/fetchGuard.js — the one browser fetch guard, and the two call paths that used to
// re-derive a request around it: stencil.load / fetchUrlToFile, and the fallback DELETE.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { guardedFetch, isFetchable, FETCHABLE_SCHEMES } from '../../js/net/fetchGuard.js';
import { deleteRemoteProject } from '../../js/net/remoteSync.js';
import { fetchUrlToFile } from '../../js/core/image/sourceLoader.js';
import { NET_TIMEOUT_MS } from '../../js/net/abortable.js';
import { installFetchStub } from '../helpers/fetchStub.js';
import { installMemoryStorage } from '../helpers/memoryStorage.js';
import { makeMockServer } from '../helpers/connectionsRig.js';

test('only the fetchable schemes pass; a local path or a file: URL never reaches fetch', async () => {
  assert.deepEqual(FETCHABLE_SCHEMES, ['http:', 'https:', 'data:', 'blob:']);
  for (const ok of ['http://h/i.png', 'HTTPS://h/i.png', 'data:image/png;base64,AA', 'blob:http://h/1']) {
    assert.equal(isFetchable(ok), true, ok);
  }
  for (const no of ['file:///etc/passwd', 'javascript:alert(1)', 'chrome-extension://x/y', '/local/path', '']) {
    assert.equal(isFetchable(no), false, no);
  }
  const stub = installFetchStub({ blob: new Blob(['x']) });
  try {
    await assert.rejects(() => guardedFetch('file:///etc/passwd'), /Refused to fetch .* http\(s\) only/);
    await assert.rejects(() => guardedFetch('../secret.png'), /no scheme/);
    assert.equal(stub.calls.length, 0, 'a refused URL is never handed to fetch');
  } finally { stub.restore(); }
});

test('every guarded request carries a deadline, and an explicit init still wins', async () => {
  const stub = installFetchStub({ blob: new Blob(['x']) });
  try {
    await guardedFetch('https://h/i.png');
    assert.ok(stub.calls[0].options.signal, `a bare call is bounded (${NET_TIMEOUT_MS}ms)`);
    const own = AbortSignal.abort();
    await guardedFetch('https://h/i.png', { mode: 'cors', signal: own });
    assert.equal(stub.calls[1].options.mode, 'cors');
    assert.equal(stub.calls[1].options.signal, own);
  } finally { stub.restore(); }
});

test('the Open-Image URL path is guarded, and keeps taking the data: URLs that field allows', async () => {
  const stub = installFetchStub({ blob: new Blob(['x'], { type: 'image/png' }) });
  try {
    await assert.rejects(() => fetchUrlToFile('file:///etc/passwd'), /Refused to fetch/);
    assert.equal(stub.calls.length, 0);
    const file = await fetchUrlToFile('https://pics.example/cat.png');
    assert.equal(file.name, 'cat.png');
    assert.ok(stub.calls[0].options.signal, 'and it is bounded');
    await fetchUrlToFile('data:image/png;base64,AA');
  } finally { stub.restore(); }
});

test('the fallback DELETE goes through a ServerConnection, not a hand-built request', async () => {
  const server = makeMockServer({ projects: [{ id: 'p_srv1_a', name: 'A', version: 0 }] });
  const storage = installMemoryStorage({
    drawingApp_servers: JSON.stringify([{ url: 'http://a:1', token: 'tkn-a' }]),
  });
  const stub = installFetchStub((url, init) => server.fetchImpl(url, init));
  try {
    await deleteRemoteProject(null, 'http://a:1', 'p_srv1_a');
    assert.equal(server.state.projects.size, 0);
    assert.deepEqual(server.state.calls, ['DELETE /projects/p_srv1_a']);
    assert.ok(stub.calls[0].options.signal, 'bounded like every other connection request');
    assert.equal(stub.calls[0].options.headers.Authorization, 'Bearer tkn-a');
    // already-gone still counts as removed
    await deleteRemoteProject(null, 'http://a:1', 'p_srv1_a');
    await assert.rejects(() => deleteRemoteProject(null, 'http://b:2', 'p_x'), /not connected/);
  } finally { stub.restore(); storage.restore(); }
});
