// ProjectTransferController (js/core/projectTransferController.js): the local ↔ server move and
// copy flows, driven against a mocked connection. Rig: helpers/projectTransferRig.js.
import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { installFetchStub } from './helpers/fetchStub.js';
import { FAKE_DATA_URL, FakeFileReader, makeConn, makeRig } from './helpers/projectTransferRig.js';

let fetchStub;
beforeEach(() => {
  globalThis.FileReader = FakeFileReader;
  fetchStub = installFetchStub({ blob: new Blob([new Uint8Array([1, 2, 3])], { type: 'image/png' }) });
});
afterEach(() => {
  delete globalThis.FileReader;
  fetchStub.restore();
});

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

