// Tests for the extension's server-connection layer (src/lib/connections.js):
// the pure pin/connection transforms plus the REST + chrome.storage wrappers,
// driven with injected fetch + a storage mock.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  normalizeUrl, isLoopbackHost, sharedPinFromProject, sharedPinsFromProjects, mergePins,
  upsertConnection, dropConnection, connect, listProjects, collectSharedPins,
  addServer, removeServer, loadConnections, CONNECTIONS_KEY,
  pinTargetMode, connectionByUrl, projectRequestFromImage, fetchProjectImage,
} from '../src/lib/connections.js';

const installStorageMock = () => {
  let store = {};
  globalThis.chrome = {
    storage: {
      local: {
        get: async (key) => (key in store ? { [key]: store[key] } : {}),
        set: async (obj) => { await Promise.resolve(); Object.assign(store, obj); },
      },
    },
  };
  return { peek: () => store, reset: () => { store = {}; } };
};

// A fake server: routes the REST subset the extension uses.
const fakeFetch = (opts = {}) => {
  const projects = opts.projects || [];
  const json = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body });
  return async (url, init = {}) => {
    const u = new URL(url);
    const method = init.method || 'GET';
    if (u.pathname === '/auth/token' && method === 'POST') return json(200, { token: 'tk', expiresAt: 0 });
    if (u.pathname === '/projects' && method === 'GET') return json(200, { projects });
    if (u.pathname === '/projects' && method === 'POST') return json(201, { id: 'p_new_a', name: JSON.parse(init.body).name });
    return json(404, { code: 'notFound', message: 'no route' });
  };
};

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
  const conn = await connect('srv:8090', '', fakeFetch());
  assert.equal(conn.url, 'https://srv:8090');
  assert.equal(conn.token, 'tk');
});

test('listProjects + collectSharedPins aggregate across servers', async () => {
  const f = (url, init) => {
    const host = new URL(url).host;
    const map = {
      'a:1': fakeFetch({ projects: [{ id: 'pa', name: 'A', hasImage: true }] }),
      'b:2': fakeFetch({ projects: [{ id: 'pb', name: 'B', hasImage: true }] }),
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
    return fakeFetch({ projects: [{ id: 'pb', name: 'B', hasImage: true }] })(url, init);
  };
  const shared = await collectSharedPins([{ url: 'http://down:0', token: 't' }, { url: 'http://b:2', token: 't' }], f);
  assert.equal(shared.length, 1);
  assert.equal(shared[0].projectId, 'pb');
});

test('addServer / removeServer persist to chrome.storage', async () => {
  const mock = installStorageMock();
  const f = fakeFetch();
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

// helper kept here to exercise createProject through the fake server
import { createProject } from '../src/lib/connections.js';
async function createProjectShim() {
  return createProject({ url: 'http://s', token: 't' }, { name: 'Pinned', source: 'http://img' }, fakeFetch());
}

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
