// credentialKind in src/lib/connections.js: how an admin credential is proved, recorded and
// re-read, and the three-way split the options list filters by.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  connect, listProjects, upsertConnection, addServer, loadConnections, CONNECTIONS_KEY,
  isAdminConnection, filterConnections, reconnectServer,
} from '../../../src/lib/connection/connections.js';
import { installStorageMock, mockFetch, tokenGatedFetch } from '../../helpers/serverMock.js';

// credentialKind: a server whose admin token can MINT session tokens but cannot list projects
// itself — the shape that proves the credential is an admin one.
const adminGatedFetch = (adminTok, sessionTok = 'sess') => async (url, init = {}) => {
  const auth = ((init.headers || {}).Authorization || '').replace('Bearer ', '');
  const json = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body });
  if (new URL(url).pathname === '/auth/token')
    return auth === adminTok ? json(200, { token: sessionTok }) : json(401, { message: 'admin required' });
  return auth === sessionTok ? json(200, { projects: [] }) : json(401, { message: 'bad token' });
};

test('connect records credentialKind admin when the mint-then-validate path proves it', async () => {
  const conn = await connect('http://a:1', 'adm', adminGatedFetch('adm'));
  assert.equal(conn.credentialKind, 'admin');
  assert.equal(conn.credential, 'adm');
  assert.equal(conn.token, 'sess', 'the row runs on the minted session token');
});

test('connect leaves credentialKind empty for a session token and for anonymous', async () => {
  // A token that lists projects straight away is an ordinary session token.
  const session = await connect('http://a:1', 'sess-tok', tokenGatedFetch('sess-tok'));
  assert.equal(session.credentialKind, '');
  // No credential at all: the server issued the token itself.
  const anon = await connect('srv:8090', '', mockFetch());
  assert.equal(anon.credentialKind, '');
  assert.equal(anon.credential, '');
});

test('an admin credential proved mid-session is recorded on the connection', async () => {
  // Server restarted: 'dead' is refused, the stored credential re-mints and the retry works.
  const conn = { url: 'http://srv:1', token: 'dead', credential: 'adm' };
  await listProjects(conn, adminGatedFetch('adm', 'fresh'));
  assert.equal(conn.token, 'fresh');
  assert.equal(conn.credentialKind, 'admin');
});

test('upsertConnection persists credentialKind, defaulting to ""', () => {
  const list = upsertConnection([], { url: 'http://a', token: 't', credentialKind: 'admin' });
  assert.equal(list[0].credentialKind, 'admin');
  assert.equal(upsertConnection([], { url: 'http://b', token: 't' })[0].credentialKind, '');
  // Anything but the exact 'admin' marker is not admin.
  assert.equal(upsertConnection([], { url: 'http://c', token: 't', credentialKind: 'ADMIN' })[0].credentialKind, '');
});

test('addServer persists credentialKind and it survives the re-read', async () => {
  const mock = installStorageMock();
  await addServer('http://a:1', 'adm', adminGatedFetch('adm'));
  await addServer('http://b:2', 'sess-tok', tokenGatedFetch('sess-tok'));
  const stored = await loadConnections();
  assert.deepEqual(stored.map((c) => [c.url, c.credentialKind]),
    [['http://b:2', ''], ['http://a:1', 'admin']]);
  assert.equal(mock.peek()[CONNECTIONS_KEY][1].credentialKind, 'admin');
  mock.reset();
});

test('reconnectServer keeps a known admin kind: only the session token is stored', async () => {
  const mock = installStorageMock();
  await addServer('http://a:1', 'adm', adminGatedFetch('adm'));
  assert.equal((await loadConnections())[0].credentialKind, 'admin');
  // Re-validating the SESSION token can never re-prove the admin credential behind it.
  const after = await reconnectServer('http://a:1', adminGatedFetch('adm'));
  assert.equal(after[0].credentialKind, 'admin');
  assert.equal((await loadConnections())[0].credentialKind, 'admin');
  mock.reset();
});

test('isAdminConnection tolerates rows saved before the field existed', () => {
  assert.equal(isAdminConnection({ url: 'http://a', token: 't' }), false);
  assert.equal(isAdminConnection({ url: 'http://a', credentialKind: '' }), false);
  assert.equal(isAdminConnection({ url: 'http://a', credentialKind: 'admin' }), true);
  assert.equal(isAdminConnection(null), false);
});

test('filterConnections splits the list three ways', () => {
  const list = [
    { url: 'http://a', credentialKind: 'admin' },
    { url: 'http://b', credentialKind: '' },
    { url: 'http://c' },                          // legacy row: no field
  ];
  assert.deepEqual(filterConnections(list, 'all').map((c) => c.url), ['http://a', 'http://b', 'http://c']);
  assert.deepEqual(filterConnections(list, 'admin').map((c) => c.url), ['http://a']);
  assert.deepEqual(filterConnections(list, 'other').map((c) => c.url), ['http://b', 'http://c']);
  // Default + junk inputs behave like 'all' / an empty list.
  assert.equal(filterConnections(list).length, 3);
  assert.equal(filterConnections(list, 'nonsense').length, 3);
  assert.deepEqual(filterConnections(null, 'admin'), []);
});
