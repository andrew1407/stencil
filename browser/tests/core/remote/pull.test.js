// js/core/remote/pull.js: the server's original is fetched before the http(s) source, a peer's
// rename lands in the store without an echo, and a download that outlives the project is dropped.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

installDom();
const { RemotePull } = await import('../../../js/core/remote/pull.js');

const PNG = () => new Blob([new Uint8Array([1])], { type: 'image/png' });

test('fetchRemoteOriginal: the stored original first, the http(s) source only when there is none', async () => {
  const fetched = [];
  const savedFetch = globalThis.fetch;
  globalThis.fetch = async (url) => { fetched.push(url); return { ok: true, blob: async () => PNG(), headers: new Map() }; };
  try {
    const pull = new RemotePull({});
    const stored = await pull.fetchRemoteOriginal({ fetchFile: async () => PNG() }, 'r1', 'https://pics/a.png');
    assert.equal(stored?.type, 'image/png');
    assert.deepEqual(fetched, [], 'the source is not asked while the server holds the original');
    const gone = { fetchFile: async () => { throw new Error('404'); } };
    assert.ok(await pull.fetchRemoteOriginal(gone, 'r1', 'https://pics/a.png'));
    assert.deepEqual(fetched, ['https://pics/a.png']);
    assert.equal(await pull.fetchRemoteOriginal(gone, 'r1', 'data:image/png;base64,AA=='), null, 'only an http(s) source is refetched');
  } finally { globalThis.fetch = savedFetch; }
});

test('adoptPeerName: renames through the store once, never for the same name or no project', () => {
  const renames = [];
  let titles = 0;
  const app = {
    activeProjectId: 'p1', imageBaseName: 'old',
    storage: { store: { getMeta: () => ({ name: 'old' }), rename: (id, name) => renames.push([id, name]) } },
    updateProjectTitle: () => { titles++; },
  };
  const pull = new RemotePull(app);
  pull.adoptPeerName('old');
  pull.adoptPeerName('');
  assert.deepEqual(renames, []);
  pull.adoptPeerName('new');
  assert.deepEqual(renames, [['p1', 'new']]);
  assert.equal(app.imageBaseName, 'new');
  assert.equal(titles, 1);
  app.activeProjectId = null;
  pull.adoptPeerName('newer');
  assert.deepEqual(renames, [['p1', 'new']], 'a temporary editor has no row to rename');
});

test('reloadPicture: a download that lands after the editor left the project loads nothing', async () => {
  let loads = 0;
  globalThis.FileReader = class { readAsDataURL() { loads++; } };
  const app = { imageBaseName: 'img', imageSource: null, storage: { temporary: true, incognito: false }, tabs: { reportActive() {} } };
  const pull = new RemotePull(app);
  const conn = { fetchFile: async () => PNG() };
  const link = { address: 'http://s', remoteId: 'r1', version: 1 };
  assert.equal(await pull.reloadPicture(conn, link, { project: {} }, () => false), false);
  assert.equal(loads, 0);
});

test('resumed: the linked record is read once and judged as an updated event; another server is not asked', async () => {
  const app = { remoteLink: { address: 'http://s', remoteId: 'r1', version: 3 } };
  const pull = new RemotePull(app);
  const judged = [];
  let reads = 0;
  const conn = { url: 'http://s', getProject: async (id) => { reads++; return { project: { id, version: 5 } }; } };
  await pull.resumed({ ...conn, url: 'http://other' }, (m) => judged.push(m));
  assert.equal(reads, 0);
  await pull.resumed(conn, (m) => judged.push(m));
  assert.deepEqual(judged, [{ type: 'project-event', event: 'updated', project: { id: 'r1', version: 5 } }]);
  await pull.resumed({ url: 'http://s', getProject: async () => { throw new Error('down'); } }, (m) => judged.push(m));
  assert.equal(judged.length, 1, 'a failed read judges nothing');
});
