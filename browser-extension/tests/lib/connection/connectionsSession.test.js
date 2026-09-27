// The session probe (GET /auth/session, falling back to one project on a server without it) and
// the bounded, ETag-conditional project list the panels refresh from (src/lib/connection/).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  connect, checkSession, listProjectsIfChanged, refreshSharedPins, collectSharedPins, SHARED_LIST_LIMIT,
} from '../../../src/lib/connection/connections.js';

const json = (status, body, headers = {}) => ({
  ok: status >= 200 && status < 300, status, json: async () => body,
  headers: { get: (k) => headers[k.toLowerCase()] ?? null },
});
// A server recording every request; `session` false = one that predates GET /auth/session.
const server = ({ session = true, good = 'tok', projects = [], etag = '"v1"' } = {}) => {
  const log = [];
  const f = async (url, init = {}) => {
    const u = new URL(url);
    const auth = ((init.headers || {}).Authorization || '').replace('Bearer ', '');
    log.push({ path: u.pathname, query: Object.fromEntries(u.searchParams), headers: init.headers || {} });
    if (u.pathname === '/auth/token') return json(auth === 'admin' ? 200 : 401, { token: good });
    if (u.pathname === '/auth/session' && !session) return json(404, { message: 'no route' });
    if (auth !== good) return json(401, { message: 'bad token' });
    if (u.pathname === '/auth/session') return json(200, { sessionId: 's1', expiresAt: 0 });
    if (u.pathname === '/projects') {
      if ((init.headers || {})['If-None-Match'] === etag) return json(304, null);
      return json(200, { projects }, { etag });
    }
    return json(404, { message: 'no route' });
  };
  return { f, log };
};

test('connect proves a pasted token with GET /auth/session, listing nothing', async () => {
  const { f, log } = server();
  const conn = await connect('http://s:1', 'tok', f);
  assert.equal(conn.token, 'tok');
  assert.deepEqual(log.map((r) => r.path), ['/auth/session']);
});

test('a server without /auth/session is asked for ONE project instead', async () => {
  const { f, log } = server({ session: false });
  await connect('http://s:1', 'tok', f);
  assert.deepEqual(log.map((r) => [r.path, r.query]), [['/auth/session', {}], ['/projects', { limit: '1' }]]);
});

test('an admin token still mints a session, which the probe then proves', async () => {
  const { f, log } = server();
  const conn = await connect('http://s:1', 'admin', f);
  assert.equal(conn.token, 'tok');
  assert.equal(conn.credentialKind, 'admin');
  assert.deepEqual(log.map((r) => r.path), ['/auth/session', '/auth/token', '/auth/session']);
});

test('checkSession rejects a dead token and never lists projects', async () => {
  const { f, log } = server();
  await assert.rejects(() => checkSession({ url: 'http://s:1', token: 'stale' }, f), (e) => e.status === 401);
  assert.ok(log.every((r) => r.path !== '/projects'));
});

test('the list is bounded, and an ETag turns an unchanged one into a 304', async () => {
  const { f, log } = server({ projects: [{ id: 'p1', hasImage: true }] });
  const conn = { url: 'http://s:1', token: 'tok' };
  const first = await listProjectsIfChanged(conn, null, f);
  assert.equal(first.changed, true);
  assert.equal(first.etag, '"v1"');
  assert.deepEqual(first.projects, [{ id: 'p1', hasImage: true }]);
  assert.deepEqual(log[0].query, { limit: String(SHARED_LIST_LIMIT) });
  assert.equal(log[0].headers['If-None-Match'], undefined);
  const again = await listProjectsIfChanged(conn, first, f);
  assert.deepEqual([again.changed, again.etag, again.projects], [false, '"v1"', null]);
  assert.equal(log[1].headers['If-None-Match'], '"v1"');
  assert.equal(log.length, 2, 'one page, one request');
});

test('refreshSharedPins reuses the cached list on a 304 and reports what changed', async () => {
  const { f } = server({ projects: [{ id: 'p1', name: 'A', hasImage: true }] });
  const conns = [{ url: 'http://s:1', token: 'tok' }];
  const cache = new Map();
  const a = await refreshSharedPins(conns, cache, f);
  assert.equal(a.changed, true, 'the first answer is news');
  assert.deepEqual(a.pins.map((p) => p.projectId), ['p1']);
  const b = await refreshSharedPins(conns, cache, f);
  assert.equal(b.changed, false);
  assert.deepEqual(b.pins.map((p) => p.projectId), ['p1'], 'the 304 keeps the rows');
  const c = await refreshSharedPins([], cache, f);
  assert.equal(c.changed, true, 'a dropped server is a change');
  assert.equal(cache.size, 0);
});

test('a server without ETags still settles to unchanged once its list stops moving', async () => {
  const { f } = server({ projects: [{ id: 'p1', hasImage: true }], etag: undefined });
  const cache = new Map();
  const conns = [{ url: 'http://s:1', token: 'tok' }];
  assert.equal((await refreshSharedPins(conns, cache, f)).changed, true);
  assert.equal((await refreshSharedPins(conns, cache, f)).changed, false);
});

test('collectSharedPins asks for the bounded list too', async () => {
  const { f, log } = server({ projects: [{ id: 'p1', hasImage: true }] });
  await collectSharedPins([{ url: 'http://s:1', token: 'tok' }], f);
  assert.equal(log[0].query.limit, String(SHARED_LIST_LIMIT));
});

test('a 30x from the server is refused, never followed with the bearer', async () => {
  const seen = [];
  const f = async (url, init) => {
    seen.push({ url, redirect: init.redirect });
    return { ok: false, status: 302, json: async () => ({}), headers: { get: () => 'http://evil:1/' } };
  };
  await assert.rejects(checkSession({ url: 'http://s:1', token: 'tok' }, f), /redirected/);
  assert.deepEqual(seen, [{ url: 'http://s:1/auth/session', redirect: 'manual' }]);
});
