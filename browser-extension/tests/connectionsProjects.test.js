// Routing a pin at a server and pulling its bytes (src/lib/connections.js): pinTargetMode,
// projectRequestFromImage, fetchProjectImage's variants, and the stale-token self-heal.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  listProjects, pinTargetMode, connectionByUrl, projectRequestFromImage, fetchProjectImage,
} from '../src/lib/connection/connections.js';

// ── pin-target selection (pure) ──

test('pinTargetMode maps connection count to a routing mode', () => {
  assert.equal(pinTargetMode([]), 'none');
  assert.equal(pinTargetMode([{ url: 'http://a' }]), 'one');
  assert.equal(pinTargetMode([{ url: 'http://a' }, { url: 'http://b' }]), 'many');
  assert.equal(pinTargetMode(null), 'none');
});

test('connectionByUrl finds the matching connection (or null)', () => {
  const conns = [{ url: 'http://a', token: 't1' }, { url: 'http://b', token: 't2' }];
  assert.equal(connectionByUrl(conns, 'http://b').token, 't2');
  assert.equal(connectionByUrl(conns, 'http://z'), null);
  assert.equal(connectionByUrl(null, 'http://a'), null);
});

test('projectRequestFromImage maps name/source/resource with fallbacks', () => {
  // src + page resource fallback
  assert.deepEqual(
    projectRequestFromImage({ name: 'Logo', src: 'http://img/logo.png' }, 'http://page'),
    { name: 'Logo', source: 'http://img/logo.png', resource: 'http://page' });
  // explicit source/resource on the record win over the fallbacks
  assert.deepEqual(
    projectRequestFromImage({ name: 'S', source: 'http://s', resource: 'http://r' }, 'http://page'),
    { name: 'S', source: 'http://s', resource: 'http://r' });
  // empty record → Untitled + empty provenance
  assert.deepEqual(projectRequestFromImage(), { name: 'Untitled', source: '', resource: '' });
});

test('fetchProjectImage defaults to the original file with Bearer auth', async () => {
  let seen = null;
  const f = async (url, init) => {
    seen = { url, headers: init.headers, method: init.method };
    return { ok: true, status: 200, blob: async () => 'IMG_BYTES' };
  };
  const blob = await fetchProjectImage({ url: 'http://srv:1', token: 'tok' }, 'p_a', 'original', f);
  assert.equal(blob, 'IMG_BYTES');
  assert.equal(seen.method, 'GET');
  assert.equal(seen.url, 'http://srv:1/projects/p_a/files/original');
  assert.equal(seen.headers.Authorization, 'Bearer tok');
});

test('fetchProjectImage with kind omitted still hits the original file', async () => {
  let seen = null;
  const f = async (url) => {
    seen = url;
    return { ok: true, status: 200, blob: async () => 'BYTES' };
  };
  await fetchProjectImage({ url: 'http://srv:1', token: 'tok' }, 'p_a', undefined, f);
  assert.equal(seen, 'http://srv:1/projects/p_a/files/original');
});

test('fetchProjectImage can request the edited result variant', async () => {
  let seen = null;
  const f = async (url) => {
    seen = url;
    return { ok: true, status: 200, blob: async () => 'RESULT_BYTES' };
  };
  const blob = await fetchProjectImage({ url: 'http://srv:1', token: 'tok' }, 'p_a', 'result', f);
  assert.equal(blob, 'RESULT_BYTES');
  assert.equal(seen, 'http://srv:1/projects/p_a/files/result');
});

test('a stale session token self-heals: re-mint with the stored credential, retry once', async () => {
  // Server restarted: 'dead' is rejected; the credential 'adm' can mint 'fresh'.
  const calls = [];
  const f = async (url, init) => {
    const auth = (init.headers.Authorization || '').replace('Bearer ', '');
    calls.push(`${init.method} ${new URL(url).pathname} [${auth}]`);
    if (url.endsWith('/auth/token')) {
      if (auth !== 'adm') return { ok: false, status: 401, json: async () => ({ message: 'admin required' }) };
      return { ok: true, status: 200, json: async () => ({ token: 'fresh' }) };
    }
    if (auth === 'fresh') return { ok: true, status: 200, json: async () => ({ projects: [] }) };
    return { ok: false, status: 401, json: async () => ({ message: 'bad token' }) };
  };
  const conn = { url: 'http://srv:1', token: 'dead', credential: 'adm' };
  const out = await listProjects(conn, f);
  assert.deepEqual(out, []);
  assert.equal(conn.token, 'fresh');   // healed in place
  assert.deepEqual(calls, [
    'GET /projects [dead]',
    'POST /auth/token [adm]',
    'GET /projects [fresh]',
  ]);
});
