// ProjectTransferController's version-guarded field push (js/core/projectTransferController.js):
// a 409 is retried with the re-read version, and an unlinked project is a no-op.
import { test, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { installFetchStub } from './helpers/fetchStub.js';
import { FakeFileReader, makeConn, makeRig } from './helpers/projectTransferRig.js';

let fetchStub;
beforeEach(() => {
  globalThis.FileReader = FakeFileReader;
  fetchStub = installFetchStub({ blob: new Blob([new Uint8Array([1, 2, 3])], { type: 'image/png' }) });
});
afterEach(() => {
  delete globalThis.FileReader;
  fetchStub.restore();
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
