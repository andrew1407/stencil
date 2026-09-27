// A server-linked project that started from a data URL: the server holds no source for it (the
// bytes are its original), so a reload from the server, or the move of the open session back to
// local, keeps the local entry's reference and the payload's full text, and the image still dedupes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../../helpers/dom.js';
import { createMemoryStorage } from '../../../helpers/memoryStorage.js';
import { makeMockServer, StubWS } from '../../../helpers/connectionsRig.js';
import { FakeFileReader, installImageDecode } from '../../../helpers/projectTransferRig.js';

installDom();
globalThis.FileReader = FakeFileReader;

const { ProjectsStore } = await import('../../../../js/core/project/store/projectsStore.js');
const { sourceRef, storedSource, keptSource } = await import('../../../../js/core/project/store/projectSources.js');
const { RemoteSyncController } = await import('../../../../js/core/remote/syncController.js');
const { ProjectTransferController } = await import('../../../../js/core/project/transferController.js');
const { ServerConnection } = await import('../../../../js/net/connectionManager.js');
const { createRemoteProject } = await import('../../../../js/net/remoteSync.js');

const A = `data:image/png;base64,${'A'.repeat(50000)}`;
const fresh = (s) => [...s].join('');
const IMG = 'data:image/png;base64,SU1H';

// The mock REST server, plus the original's bytes back on GET.
const serverWithFiles = () => {
  const server = makeMockServer();
  const rest = server.fetchImpl;
  server.fetchImpl = async (url, init = {}) => {
    if ((init.method || 'GET') === 'GET' && /\/files\/original$/.test(new URL(url).pathname)) {
      return { ok: true, status: 200, blob: async () => new Blob([new Uint8Array([1, 2, 3])], { type: 'image/png' }) };
    }
    return rest(url, init);
  };
  return server;
};

// A local project made from the data URL, then linked to a server project created from it.
const linkedProject = async () => {
  const server = serverWithFiles();
  const conn = new ServerConnection('http://srv:9', { token: 't', fetchImpl: server.fetchImpl, WebSocketImpl: StubWS });
  await conn.handshake();
  const store = new ProjectsStore(createMemoryStorage());
  const app = { activeProjectId: 'p1', imageSource: A, image: null, storage: { store }, connections: { get: () => conn } };
  const persist = () => store.upsert(
    { ...(store.getMeta(app.activeProjectId) || { name: 'shot', thumbnail: null }), id: app.activeProjectId, source: app.imageSource },
    { image: IMG, layout: { imageSource: app.imageSource } });
  persist();
  app.remoteLink = await createRemoteProject(conn, { name: 'shot', source: A, bytes: new Uint8Array([1, 2, 3]), w: 2, h: 2 });
  store.upsert({ ...store.getMeta('p1'), address: conn.url, remoteId: app.remoteLink.remoteId }, store.get('p1').payload);
  return { server, conn, store, app, persist };
};

test('the server row\'s source wins only when it is a URL', () => {
  assert.equal(keptSource('https://x/a.png', A), 'https://x/a.png');
  assert.equal(keptSource('', A), A);
  assert.equal(keptSource(sourceRef(A), ''), sourceRef(A));
  assert.equal(keptSource('', null), '');
});

test('a reload from the server keeps the dedupe of a project made from a data URL', async () => {
  const { server, store, app, persist } = await linkedProject();
  assert.equal(server.state.projects.get(app.remoteLink.remoteId).source, '', 'the server holds no source');
  // The real load flow (core/image/loadFlow.js) takes the kept source; the decode persists it as the save does.
  const decode = installImageDecode(() => persist());
  try {
    await new RemoteSyncController(app).reloadRemoteActive();
    await new Promise((r) => setImmediate(r));
  } finally { decode.restore(); }
  assert.equal(decode.reads().length, 1, 'the picture reloaded from the server');
  assert.equal(app.imageSource, A);
  assert.deepEqual(store.findByImage(fresh(A), 'other').map((m) => m.id), ['p1'], 'opening it again finds the project');
  assert.equal(store.getMeta('p1').source, sourceRef(A));
  assert.equal(storedSource(store, 'p1'), A, 'the payload keeps the full text');
});

test('moving the open linked session back to local keeps its reference and full text', async () => {
  const { conn, store, app } = await linkedProject();
  const host = { ...app, loadImageFromFile() {} };
  const ctrl = new ProjectTransferController({
    storage: { store, temporary: false, incognito: false, save() {} },
    tabs: { projectsChanged() {}, reportActive() {} },
    remoteSync: { fetchRemoteOriginal: async () => new Blob([new Uint8Array([9])], { type: 'image/png' }) },
    getConnections: () => ({ get: () => conn }),
    host,
  });
  ctrl.switchToProject = () => true;
  const newId = await ctrl.moveProjectToLocal({ id: app.remoteLink.remoteId, serverUrl: conn.url, name: 'shot' });
  assert.notEqual(newId, 'p1');
  assert.equal(store.getMeta('p1'), null, 'the linked cache entry is gone');
  assert.deepEqual(store.findByImage(fresh(A), '').map((m) => m.id), [newId]);
  assert.equal(storedSource(store, newId), A);
});
