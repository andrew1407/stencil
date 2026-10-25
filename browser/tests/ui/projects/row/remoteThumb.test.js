// Which stored file a server-project row asks for (js/ui/projects/row/remoteRow.js): the record
// carries flags, never storage paths, so the choice reads `hasResult` and `hasImage`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { remoteThumbBlob } from '../../../../js/ui/projects/row/remoteRow.js';

let seq = 0;
const rig = (fail = new Set()) => {
  const asked = [];
  const conn = {
    fetchFile: async (id, kind) => {
      asked.push(kind);
      if (fail.has(kind)) throw new Error('404');
      return `${id}:${kind}`;
    },
  };
  const meta = (extra) => ({ id: `p${++seq}`, serverUrl: 'https://s', version: 1, ...extra });
  return { asked, conn, meta };
};

test('an edited project shows its stored result', async () => {
  const { asked, conn, meta } = rig();
  const m = meta({ hasImage: true, hasResult: true });
  assert.equal(await remoteThumbBlob(conn, m), `${m.id}:result`);
  assert.deepEqual(asked, ['result']);
});

test('a project with only an original shows the original', async () => {
  const { asked, conn, meta } = rig();
  const m = meta({ hasImage: true });
  assert.equal(await remoteThumbBlob(conn, m), `${m.id}:original`);
  assert.deepEqual(asked, ['original']);
});

test('a result that fails to load falls back to the original', async () => {
  const { asked, conn, meta } = rig(new Set(['result']));
  const m = meta({ hasImage: true, hasResult: true });
  assert.equal(await remoteThumbBlob(conn, m), `${m.id}:original`);
  assert.deepEqual(asked, ['result', 'original']);
});

test('a project with no stored file makes no request', async () => {
  const { asked, conn, meta } = rig();
  assert.equal(await remoteThumbBlob(conn, meta({ hasImage: false })), null);
  assert.deepEqual(asked, []);
});
