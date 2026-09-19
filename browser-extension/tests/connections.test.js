// The extension's server-connection layer (src/lib/connections.js): the pure pin/connection
// transforms plus the REST + chrome.storage wrappers, over an injected fetch and a storage mock.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  normalizeUrl, isLoopbackHost, sharedPinFromProject, sharedPinsFromProjects, mergePins,
  upsertConnection, dropConnection, connect, listProjects, collectSharedPins,
  addServer, removeServer, loadConnections, CONNECTIONS_KEY, createProject,
  parseInviteUrl,
} from '../src/lib/connections.js';
import { installStorageMock, mockFetch, tokenGatedFetch } from './helpers/serverMock.js';

test('normalizeUrl is secure by default: bare remote → https, loopback → http', () => {
  assert.equal(normalizeUrl('host:8090'), 'https://host:8090');
  assert.equal(normalizeUrl('localhost:8090'), 'http://localhost:8090');
  assert.equal(normalizeUrl('127.0.0.1:8090'), 'http://127.0.0.1:8090');
  assert.equal(normalizeUrl('http://h:1/projects'), 'http://h:1');
  assert.throws(() => normalizeUrl(''));
});

test('isLoopbackHost classifies hosts like the browser client', () => {
  assert.equal(isLoopbackHost('localhost'), true);
  assert.equal(isLoopbackHost('127.0.0.1'), true);
  assert.equal(isLoopbackHost('::1'), true);
  assert.equal(isLoopbackHost('example.com'), false);
});

test('sharedPinFromProject marks shared + carries serverUrl/projectId', () => {
  const pin = sharedPinFromProject({ id: 'p_a_b', name: 'Shot', resource: 'http://pg', updatedAt: 9 }, 'http://srv:1');
  assert.equal(pin.shared, true);
  assert.equal(pin.serverUrl, 'http://srv:1');
  assert.equal(pin.projectId, 'p_a_b');
  assert.equal(pin.site, 'http://srv:1');
  assert.equal(pin.name, 'Shot');
  assert.match(pin.source, /\/projects\/p_a_b\/files\/original$/);
});

test('sharedPinFromProject carries the project color (defaulting to "")', () => {
  const tinted = sharedPinFromProject({ id: 'p_c', name: 'Tinted', color: '#12ab34', hasImage: true }, 'http://srv:1');
  assert.equal(tinted.color, '#12ab34');
  // No color on the project → "" (the popup falls back to the neutral muted grey).
  const plain = sharedPinFromProject({ id: 'p_d', name: 'Plain' }, 'http://srv:1');
  assert.equal(plain.color, '');
});

test('sharedPinsFromProjects keeps only image projects', () => {
  const pins = sharedPinsFromProjects([
    { id: 'p1', name: 'A', hasImage: true },
    { id: 'p2', name: 'B', hasImage: false },
  ], 'http://s');
  assert.equal(pins.length, 1);
  assert.equal(pins[0].projectId, 'p1');
});

test('mergePins tags local shared:false, appends deduped shared', () => {
  const local = [{ site: 'http://a', source: 'x', name: 'L' }];
  const shared = [
    { serverUrl: 'http://s', projectId: 'p1', name: 'S1', shared: true },
    { serverUrl: 'http://s', projectId: 'p1', name: 'dup', shared: true },
    { serverUrl: 'http://s', projectId: 'p2', name: 'S2', shared: true },
  ];
  const merged = mergePins(local, shared);
  assert.equal(merged.length, 3); // 1 local + 2 distinct shared
  assert.equal(merged[0].shared, false);
  assert.equal(merged[1].projectId, 'p1');
  assert.equal(merged[2].projectId, 'p2');
});

test('upsertConnection / dropConnection key on url', () => {
  let list = upsertConnection([], { url: 'http://a', token: 't1' });
  list = upsertConnection(list, { url: 'http://b', token: 't2' });
  list = upsertConnection(list, { url: 'http://a', token: 't1b' }); // replace, float to front
  assert.deepEqual(list.map((c) => c.url), ['http://a', 'http://b']);
  assert.equal(list[0].token, 't1b');
  list = dropConnection(list, 'http://a');
  assert.deepEqual(list.map((c) => c.url), ['http://b']);
});

test('connect issues a token when none supplied', async () => {
  const conn = await connect('srv:8090', '', mockFetch());
  assert.equal(conn.url, 'https://srv:8090');
  assert.equal(conn.token, 'tk');
});

// ── Invite links: `<url>#token=<value>` (browser connectionManager parity) ──
test('parseInviteUrl splits the #token= fragment off the URL', () => {
  assert.deepEqual(parseInviteUrl('http://localhost:8090#token=abc123'),
    { url: 'http://localhost:8090', token: 'abc123' });
  assert.deepEqual(parseInviteUrl('srv:8090#token=a%2Bb'), { url: 'srv:8090', token: 'a+b' });
  // No fragment, a non-token fragment, or an empty token → pass through untouched.
  assert.deepEqual(parseInviteUrl('http://h:1'), { url: 'http://h:1', token: '' });
  assert.deepEqual(parseInviteUrl('http://h:1#other=x'), { url: 'http://h:1#other=x', token: '' });
  assert.deepEqual(parseInviteUrl('http://h:1#token='), { url: 'http://h:1#token=', token: '' });
});

test('connect with an invite link strips the fragment and adopts the token as credential', async () => {
  const conn = await connect('http://a:1#token=sess-tok', '', tokenGatedFetch('sess-tok'));
  assert.equal(conn.url, 'http://a:1', 'the fragment never reaches the url');
  assert.equal(conn.token, 'sess-tok');
  assert.equal(conn.credential, 'sess-tok', 'the fragment token feeds the credential flow');
});

test('an explicitly supplied token wins over the invite fragment', async () => {
  const conn = await connect('http://a:1#token=bogus', 'real-tok', tokenGatedFetch('real-tok'));
  assert.equal(conn.token, 'real-tok');
  assert.equal(conn.credential, 'real-tok');
});

test('listProjects + collectSharedPins aggregate across servers', async () => {
  const f = (url, init) => {
    const host = new URL(url).host;
    const map = {
      'a:1': mockFetch({ projects: [{ id: 'pa', name: 'A', hasImage: true }] }),
      'b:2': mockFetch({ projects: [{ id: 'pb', name: 'B', hasImage: true }] }),
    };
    return map[host](url, init);
  };
  const conns = [{ url: 'http://a:1', token: 't' }, { url: 'http://b:2', token: 't' }];
  const got = await listProjects(conns[0], f);
  assert.equal(got[0].id, 'pa');
  const shared = await collectSharedPins(conns, f);
  assert.deepEqual(shared.map((p) => p.projectId).sort(), ['pa', 'pb']);
});

test('collectSharedPins survives an unreachable server', async () => {
  const f = (url, init) => {
    if (new URL(url).host === 'down:0') throw new Error('refused');
    return mockFetch({ projects: [{ id: 'pb', name: 'B', hasImage: true }] })(url, init);
  };
  const shared = await collectSharedPins([{ url: 'http://down:0', token: 't' }, { url: 'http://b:2', token: 't' }], f);
  assert.equal(shared.length, 1);
  assert.equal(shared[0].projectId, 'pb');
});

test('addServer / removeServer persist to chrome.storage', async () => {
  const mock = installStorageMock();
  const f = mockFetch();
  await addServer('srv:8090', '', f);
  let stored = (await loadConnections());
  assert.equal(stored.length, 1);
  assert.equal(stored[0].url, 'https://srv:8090');
  assert.equal(mock.peek()[CONNECTIONS_KEY][0].token, 'tk');

  const after = await removeServer('https://srv:8090');
  assert.equal(after.length, 0);
  mock.reset();
});

test('createProject posts and returns the new record', async () => {
  const rec = await createProjectShim();
  assert.equal(rec.id, 'p_new_a');
  assert.equal(rec.name, 'Pinned');
});

// helper kept here to exercise createProject through the mock server
async function createProjectShim() {
  return createProject({ url: 'http://s', token: 't' }, { name: 'Pinned', source: 'http://img' }, mockFetch());
}
