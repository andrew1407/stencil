// js/net/cappedBody.js: a reply is read up to its cap, declared or streamed, and never past it;
// the LLM client's replies and a server's REST JSON both come through it. Real Response
// objects over endless streams stand in for a hostile server.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MAX_ERROR_BYTES, MAX_FETCH_BYTES, readCapped, readJsonCapped } from '../../js/net/cappedBody.js';
import { MAX_REPLY_BYTES, postJson } from '../../js/llm/http.js';

// A body that never ends: the reader must stop it, not buffer it.
const endless = () => new ReadableStream({ pull(c) { c.enqueue(new Uint8Array(1024)); } });

test('the caps are 64 MiB for a reply, 64 KiB for an error body, 8 MiB for a provider', () => {
  assert.deepEqual([MAX_FETCH_BYTES, MAX_ERROR_BYTES, MAX_REPLY_BYTES], [64 << 20, 64 << 10, 8 << 20]);
});

test('JSON within the cap parses, a byte-order mark and all', async () => {
  assert.deepEqual(await readJsonCapped(new Response('﻿{"a":[1,2]}')), { a: [1, 2] });
  assert.deepEqual([...new Uint8Array(await readCapped(new Response('abc'), 3))], [97, 98, 99]);
});

test('a declared or a streamed body past the cap is refused without reading it all', async () => {
  const declared = new Response('{}', { headers: { 'content-length': String(MAX_FETCH_BYTES + 1) } });
  await assert.rejects(() => readJsonCapped(declared), /exceeds the 67108864-byte fetch cap/);
  await assert.rejects(() => readJsonCapped(new Response(endless()), 4096), /4096-byte fetch cap/);
});

test('a plain object with no stream (an injected fetch) reads through its own json()', async () => {
  assert.deepEqual(await readJsonCapped({ json: async () => ({ ok: 1 }) }), { ok: 1 });
});

test('a provider reply past 8 MiB fails the POST; an oversize error body falls back to the status', async () => {
  const huge = async () => new Response(endless(), { status: 200 });
  await assert.rejects(() => postJson(huge, 'https://llm.test/v1', {}), /8388608-byte fetch cap/);
  const loudError = async () => new Response(endless(), { status: 500 });
  await assert.rejects(() => postJson(loudError, 'https://llm.test/v1', {}), (e) => e.status === 500);
});
