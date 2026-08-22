// ProjectTransferController (js/core/projectTransferController.js): the local ↔ server
// move/copy flows and the version-guarded field push, driven against a mocked server
// connection — the unit coverage its extraction from drawingApp.js makes possible.

import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { ProjectTransferController } from '../js/core/projectTransferController.js';
import { installFetchStub } from './helpers/fetchStub.js';

// #blobToDataUrl runs through FileReader, which Node lacks: a minimal stand-in that
// "reads" any blob to a fixed data URL.
const FAKE_DATA_URL = 'data:image/png;base64,ZmFrZQ==';
class FakeFileReader {
  readAsDataURL() {
    this.result = FAKE_DATA_URL;
    queueMicrotask(() => this.onload && this.onload());
  }
}

let fetchStub;
beforeEach(() => {
  globalThis.FileReader = FakeFileReader;
  fetchStub = installFetchStub({ blob: new Blob([new Uint8Array([1, 2, 3])], { type: 'image/png' }) });
});
afterEach(() => {
  delete globalThis.FileReader;
  fetchStub.restore();
});

// A recording server connection covering the REST surface the transfer flows touch.
const makeConn = (url = 'https://srv.example', over = {}) => {
  const conn = {
    url,
    calls: [],
    createProject: async (fields) => { conn.calls.push(['createProject', fields]); return { id: 'r1', version: 1 }; },
    putFile: async (id, kind, bytes, meta) => { conn.calls.push(['putFile', id, kind, bytes, meta]); },
    updateProject: async (id, fields) => { conn.calls.push(['updateProject', id, fields]); return { id, version: (fields.version || 0) + 1 }; },
    getProject: async (id) => { conn.calls.push(['getProject', id]); return { project: { id, name: 'Remote', version: 7, source: '', color: '#112233' }, layout: { imageWidth: 4, imageHeight: 3, lines: [] } }; },
    deleteProject: async (id) => { conn.calls.push(['deleteProject', id]); },
    ...over,
  };
  return conn;
};

// A recording projects-store + storage + tabs + host rig with just what the flows read.
const makeRig = ({ conn, meta = {}, payload = {} } = {}) => {
  const calls = [];
  const store = {
    get: (id) => ({ id, payload }),
    getMeta: () => ({ id: 'p1', name: 'Local', ...meta }),
    upsert: (m, p) => { calls.push(['upsert', m, p]); },
    createId: () => 'new-local',
    remove: (id) => { calls.push(['remove', id]); },
    list: () => [],
  };
  const storage = {
    temporary: false,
    incognito: false,
    store,
    save: () => { calls.push(['storage.save']); },
    newTemporary: () => { calls.push(['storage.newTemporary']); },
    loadProject: (id) => { calls.push(['storage.loadProject', id]); return true; },
  };
  const tabs = {
    projectsChanged: (d) => { calls.push(['projectsChanged', d]); },
    reportActive: (id) => { calls.push(['reportActive', id]); },
  };
  const remoteSync = {
    fetchRemoteOriginal: async () => new Blob([new Uint8Array([9])], { type: 'image/png' }),
    reloadRemoteActive: () => { calls.push(['reloadRemoteActive']); },
  };
  const host = {
    activeProjectId: null,
    remoteLink: null,
    blankColor: '',
    imageBaseName: '',
    chatPersistence: null,
    updateProjectTitle: () => { calls.push(['updateProjectTitle']); },
    updateIncognitoUI: () => { calls.push(['updateIncognitoUI']); },
    newEditor: () => { calls.push(['newEditor']); },
    loadImageFromFile: (file, opts) => { calls.push(['loadImageFromFile', file, opts]); },
    setBlankColor: (c) => { calls.push(['setBlankColor', c]); },
  };
  const ctrl = new ProjectTransferController({
    storage, tabs, remoteSync,
    getConnections: () => ({ get: (addr) => (addr === conn.url ? conn : null) }),
    host,
  });
  return { ctrl, calls, store, storage, tabs, host };
};

test('moveProjectToServer creates the project remotely and links the local copy', async () => {
  const conn = makeConn();
  const { ctrl, calls, host } = makeRig({
    conn,
    meta: { color: '#abcdef', source: 'https://img/x.png' },
    payload: { image: FAKE_DATA_URL, layout: { imageWidth: 10, imageHeight: 20, lines: [{ points: [] }] } },
  });
  host.activeProjectId = 'p1';

  const remoteId = await ctrl.moveProjectToServer('p1', conn.url);

  assert.equal(remoteId, 'r1');
  // The active project is flushed before its bytes are read.
  assert.deepEqual(calls[0], ['storage.save']);
  const kinds = conn.calls.map(c => c[0]);
  assert.deepEqual(kinds, ['createProject', 'putFile', 'getProject', 'updateProject'],
    'create + original upload + version re-read + layout push');
  assert.equal(conn.calls[0][1].name, 'Local');
  assert.equal(conn.calls[0][1].color, '#abcdef');
  assert.equal(conn.calls[1][3] instanceof Uint8Array, true, 'the data URL was decoded to raw bytes');
  // The local record is relinked (same id, server address + remote id + bumped version).
  const upsert = calls.find(c => c[0] === 'upsert');
  assert.equal(upsert[1].id, 'p1');
  assert.equal(upsert[1].address, conn.url);
  assert.equal(upsert[1].remoteId, 'r1');
  assert.equal(upsert[1].remoteVersion, 8, 'adopts the layout save\'s bumped version (7 + 1)');
  // The open session follows the link and the UI repaints.
  assert.deepEqual(host.remoteLink, { address: conn.url, remoteId: 'r1', version: 8 });
  assert.ok(calls.some(c => c[0] === 'updateProjectTitle'));
  assert.ok(calls.some(c => c[0] === 'projectsChanged' && c[1].id === 'p1'));
});

test('copyProjectToServer defaults the name to "<name>-copy" and leaves the local project untouched', async () => {
  const conn = makeConn();
  const { ctrl, calls, host } = makeRig({ conn, payload: {} });
  host.activeProjectId = 'other';

  const remoteId = await ctrl.copyProjectToServer('p1', conn.url);

  assert.equal(remoteId, 'r1');
  assert.equal(conn.calls[0][1].name, 'Local-copy');
  assert.ok(!calls.some(c => c[0] === 'upsert'), 'a copy never rewrites the local record');
  assert.equal(host.remoteLink, null, 'the session is not relinked by a copy');
  // No image payload → no original upload; the layout push still lands.
  assert.deepEqual(conn.calls.map(c => c[0]), ['createProject', 'updateProject']);
});

test('an explicit copy name overrides the "-copy" default', async () => {
  const conn = makeConn();
  const { ctrl } = makeRig({ conn, payload: {} });
  await ctrl.copyProjectToServer('p1', conn.url, { name: 'Renamed' });
  assert.equal(conn.calls[0][1].name, 'Renamed');
});

test('moveProjectToLocal imports the server project, deletes it remotely, and follows the open session', async () => {
  const conn = makeConn();
  const { ctrl, calls, host } = makeRig({ conn });
  host.activeProjectId = 'cache-1';
  host.remoteLink = { address: conn.url, remoteId: 'srv-9', version: 3 };

  const newId = await ctrl.moveProjectToLocal({ id: 'srv-9', serverUrl: conn.url, name: 'Remote' });

  assert.equal(newId, 'new-local');
  assert.ok(conn.calls.some(c => c[0] === 'deleteProject' && c[1] === 'srv-9'), 'a move removes the server copy');
  const upsert = calls.find(c => c[0] === 'upsert');
  assert.equal(upsert[1].id, 'new-local');
  assert.equal(upsert[1].name, 'Remote', 'a move keeps the server name (no -copy)');
  assert.equal(upsert[1].address, null, 'the import is detached — no server link');
  assert.equal(upsert[1].remoteId, null);
  assert.equal(upsert[2].image, FAKE_DATA_URL, 'the fetched original is stored as a data URL');
  // The stale local cache of the open session is dropped and the editor switches over.
  assert.ok(calls.some(c => c[0] === 'remove' && c[1] === 'cache-1'));
  assert.ok(calls.some(c => c[0] === 'storage.loadProject' && c[1] === 'new-local'));
});

test('copyServerProjectToLocal keeps the server copy and names the local one "<base>-copy"', async () => {
  const conn = makeConn();
  const { ctrl, calls } = makeRig({ conn });

  const newId = await ctrl.copyServerProjectToLocal({ id: 'srv-9', serverUrl: conn.url, name: 'Remote' });

  assert.equal(newId, 'new-local');
  assert.ok(!conn.calls.some(c => c[0] === 'deleteProject'), 'a copy leaves the server project in place');
  const upsert = calls.find(c => c[0] === 'upsert');
  assert.equal(upsert[1].name, 'Remote-copy');
  // Page format + formulas defaults are carried into the detached layout.
  assert.equal(upsert[2].layout.pageSize, 'A3');
  assert.equal(upsert[2].layout.imageWidth, 4);
});

test('copyServerProjectToIncognito flushes, resets, and loads the image as an unlinked incognito session', async () => {
  const conn = makeConn();
  const { ctrl, calls, storage } = makeRig({ conn });

  await ctrl.copyServerProjectToIncognito({ id: 'srv-9', serverUrl: conn.url });

  assert.equal(storage.incognito, true);
  const order = calls.map(c => c[0]);
  assert.ok(order.indexOf('storage.save') < order.indexOf('newEditor'), 'current project flushed first');
  const load = calls.find(c => c[0] === 'loadImageFromFile');
  assert.ok(load[1] instanceof File);
  assert.equal(load[2].adoptLayout, true, 'annotations adopt without linking');
  assert.equal(load[2].remoteId, undefined, 'no server link rides the incognito copy');
});

test('transfer to an unconnected server rejects up front', async () => {
  const conn = makeConn();
  const { ctrl } = makeRig({ conn });
  await assert.rejects(() => ctrl.moveProjectToServer('p1', 'https://elsewhere.example'), /Not connected/);
  assert.equal(conn.calls.length, 0);
});

test('pushProjectFieldToServer retries a 409 with the re-read version and adopts the bump', async () => {
  let failures = 1;
  const conn = makeConn('https://srv.example', {
    updateProject: async (id, fields) => {
      conn.calls.push(['updateProject', id, fields]);
      if (failures-- > 0) { const e = new Error('conflict'); e.status = 409; throw e; }
      return { id, version: fields.version + 1 };
    },
  });
  const { ctrl, host } = makeRig({ conn });
  host.activeProjectId = 'p1';
  host.remoteLink = { address: conn.url, remoteId: 'srv-9', version: 2 };

  await ctrl.pushProjectFieldToServer('p1', { color: '#123456' }, 'fail');

  const updates = conn.calls.filter(c => c[0] === 'updateProject');
  assert.equal(updates.length, 2);
  assert.equal(updates[0][2].version, 2, 'first attempt uses the cached version');
  assert.equal(updates[1][2].version, 7, 'retry uses the server\'s re-read version');
  assert.equal(host.remoteLink.version, 8, 'the bumped version is adopted onto the live link');
});

test('pushProjectFieldToServer is a no-op for a project with no server link', async () => {
  const conn = makeConn();
  const { ctrl } = makeRig({ conn, meta: {} });
  await ctrl.pushProjectFieldToServer('p1', { color: '#123456' }, 'fail');
  assert.equal(conn.calls.length, 0);
});
