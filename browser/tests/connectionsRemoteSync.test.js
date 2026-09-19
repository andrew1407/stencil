// remoteSync (js/net/remoteSync.js): create-on-server and save-back route through the REST
// calls, track the version they get back, and flag a 409 as a conflict.
import { test } from 'node:test';
import assert from 'node:assert';
import { ServerConnection, ConnectionManager } from '../js/net/connectionManager.js';
import {
  requireConnection, createRemoteProject, saveRemoteProject, CONFLICT_MESSAGE,
} from '../js/net/remoteSync.js';
import { makeMockServer, StubWS } from './helpers/connectionsRig.js';

// ── remoteSync: create-on-server + save-back helpers ──
const connectOne = async (server, url = 'http://srv:9') => {
  const c = new ServerConnection(url, { token: 't', fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await c.handshake();
  return c;
};

test('requireConnection validates a live connection or throws clearly', async () => {
  const server = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect('http://srv:8090');
  assert.equal(requireConnection(mgr, 'http://srv:8090').url, 'http://srv:8090');
  assert.throws(() => requireConnection(mgr, 'http://nope:1'), /Not connected/);
  assert.throws(() => requireConnection(null, 'x'), /No server connections/);
});

test('createRemoteProject routes to createProject + putFile(original) and tracks version', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, {
    name: 'Shot', source: 'http://x/a.png', bytes: new Uint8Array([1, 2, 3]), ext: 'png', w: 4, h: 3,
  });
  assert.equal(link.address, 'http://srv:9');
  assert.match(link.remoteId, /^p_srv/);
  assert.equal(link.version, 1, 'create (v0) then original upload (→v1)');
  assert.ok(server.state.calls.includes('POST /projects'));
  assert.ok(server.state.calls.some((c) => /^POST \/projects\/.+\/files\/original$/.test(c)));
  assert.equal(server.state.bodies[0].kind, 'original');
  const rec = server.state.projects.get(link.remoteId);
  assert.equal(rec.hasImage, true);
  assert.equal(rec.imageW, 4);
});

test('createRemoteProject with no bytes creates a blank project (no upload)', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, { name: 'Empty' });
  assert.equal(link.version, 0);
  assert.ok(!server.state.calls.some((c) => c.includes('/files/')));
  assert.equal(server.state.projects.get(link.remoteId).hasImage, false);
});

test('saveRemoteProject routes to updateProject + putFile(result) and tracks version', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, { name: 'P', bytes: new Uint8Array([9]), w: 2, h: 2 });
  const next = await saveRemoteProject(conn, link, {
    name: 'P2', layout: { lines: [] }, bytes: new Uint8Array([7, 7]), w: 2, h: 2,
  });
  assert.equal(next.version, 3, 'update (→v2) then result upload (→v3)');
  assert.ok(server.state.calls.some((c) => /^PUT \/projects\//.test(c)));
  assert.ok(server.state.calls.some((c) => /\/files\/result$/.test(c)));
  const rec = server.state.projects.get(link.remoteId);
  assert.equal(rec.name, 'P2');
  assert.deepEqual(rec.layout, { lines: [] });
});

test('saveRemoteProject surfaces a 409 as a flagged conflict error', async () => {
  const server = makeMockServer();
  const conn = await connectOne(server);
  const link = await createRemoteProject(conn, { name: 'P', bytes: new Uint8Array([1]) });
  const stale = { ...link, version: 0 };   // server is at v1; v0 guard is stale
  await assert.rejects(
    () => saveRemoteProject(conn, stale, { name: 'x', layout: {} }),
    (err) => { assert.equal(err.conflict, true); assert.equal(err.message, CONFLICT_MESSAGE); return true; },
  );
});

test('move-to-server contract: adopt saveRemoteProject\'s refreshed version so a later field push does not 409', async () => {
  // The layout save advances the server version again, so the link persisted on the editor is the one
  // saveRemoteProject returns, not the create link — a stale version 409s the next push.
  const server = makeMockServer();
  const conn = await connectOne(server);
  const createLink = await createRemoteProject(conn, { name: 'P', bytes: new Uint8Array([1, 2]), w: 2, h: 2 });
  const savedLink = await saveRemoteProject(conn, createLink, { name: 'P', layout: { lines: [] } });
  assert.ok(savedLink.version > createLink.version, 'the layout save advances the server version past create time');

  // A field push with the REFRESHED version succeeds…
  const ok = await conn.updateProject(createLink.remoteId, { name: 'renamed', version: savedLink.version });
  assert.equal(ok.version, savedLink.version + 1);
  // …while the STALE create-time version 409s (the exact failure this guards).
  await assert.rejects(
    () => conn.updateProject(createLink.remoteId, { name: 'again', version: createLink.version }),
    (err) => err.status === 409,
  );
});

test('deleteProject removes a project on the server', async () => {
  const server = makeMockServer({ projects: [{ id: 'p_a_b', name: 'X', version: 0 }] });
  const conn = await connectOne(server);
  await conn.deleteProject('p_a_b');
  assert.ok(!server.state.projects.has('p_a_b'));
  assert.ok(server.state.calls.includes('DELETE /projects/p_a_b'));
});
