// The call deadlines and window scoping of src/content/editorApiMain.js: the two timeout tiers,
// a late or foreign reply, and the one-way hand-offs that carry no id at all.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MSG, SRC, answer, lastCall, loadApi } from '../helpers/editorApiEnv.js';

// The call deadline is the OUTER one, bounding bridge → worker → another tab's page → network,
// each leg on its own 1500 ms budget. Two tiers: single-hop, and fan-out/fetch.
const SINGLE_HOP_MS = 4000;
const SLOW_CALL_MS = 30000;

test('a bridge that never answers rejects the call after the timeout', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { ext } = await loadApi();
  const p = ext.focus(3);

  t.mock.timers.tick(SINGLE_HOP_MS - 1);
  assert.equal(await Promise.race([p.then(() => 'settled', () => 'settled'), Promise.resolve('pending')]), 'pending');
  t.mock.timers.tick(1);
  const err = await p.then(() => null, (e) => e);
  assert.match(err.message, /the Stencil extension did not answer/);
});

test('a call that fans out (or pulls image bytes) is given far longer than one page hop', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const env = await loadApi();
  const p = env.ext.editors();

  // The worker spends a full 1500 ms page wait on each silent editor tab before it can
  // answer with their `ready:false` rows — the outer deadline must not fire first.
  t.mock.timers.tick(SINGLE_HOP_MS);
  answer(env, { ok: true, editors: [{ tabId: 3, ready: false }] });
  assert.deepEqual((await p).map((r) => r.tabId), [3]);

  const slow = env.ext.editors();
  t.mock.timers.tick(SLOW_CALL_MS);
  const err = await slow.then(() => null, (e) => e);
  assert.match(err.message, /the Stencil extension did not answer/);
});

test('a late reply after the timeout never re-enters the settled call', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const env = await loadApi();
  const p = env.ext.editors();
  const { id } = lastCall(env.posted);

  t.mock.timers.tick(SLOW_CALL_MS);
  await p.then(() => null, () => null);
  env.dispatch({ source: SRC.EXT_API_RES, id, ok: true, result: { ok: true, editors: [] } });   // no throw
});

test('replies from another window are ignored', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const env = await loadApi();
  const p = env.ext.editors();
  const { id } = lastCall(env.posted);

  env.dispatch({ source: SRC.EXT_API_RES, id, ok: true, result: { ok: true, editors: [{ tabId: 1 }] } }, {});
  t.mock.timers.tick(SLOW_CALL_MS);
  const err = await p.then(() => null, (e) => e);
  assert.match(err.message, /the Stencil extension did not answer/);
});

test('openInNewTab() and crop() are one-way hand-offs: posted without an id, chainable', async () => {
  const env = await loadApi();

  assert.equal(env.ext.openInNewTab('http://cdn/a.png', { incognito: true }), env.ext);
  let sent = lastCall(env.posted);
  assert.equal(sent.id, undefined, 'no id = the bridge relays and waits for nothing');
  assert.deepEqual(sent.message, { type: MSG.PAGE_OPEN, url: 'http://cdn/a.png', name: 'a.png', source: 'http://cdn/a.png', resource: '', incognito: true, newTab: true });

  env.ext.crop('http://cdn/b.png', { album: true });
  sent = lastCall(env.posted);
  assert.equal(sent.message.type, MSG.PAGE_CROP);
  assert.equal(sent.message.url, 'http://cdn/b.png');
  assert.equal(sent.message.album, true);
});
