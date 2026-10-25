// A session-only credential (js/net/connectionManager.js `saved`): a launch link's differing token
// connects for this session, while the snapshot — what is persisted — keeps the saved credential,
// and a refusal of the link's token never marks the saved one expired.
import { test } from 'node:test';
import assert from 'node:assert';
import { ConnectionManager } from '../../js/net/connectionManager.js';
import { makeMockServer, StubWS } from '../helpers/connectionsRig.js';

test('the live connection uses the link\'s token; the snapshot keeps the saved credential and its kind', async () => {
  const { fetchImpl } = makeMockServer();
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  await mgr.connect({ url: 'http://a:1', token: 'link-token', saved: { token: 'saved-admin', kind: 'admin' } });
  assert.strictEqual(mgr.get('http://a:1').credential, 'link-token');
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'saved-admin', kind: 'admin' }]);
});

test('a refused session-only token leaves the saved credential\'s expired row as it was', async () => {
  const { fetchImpl } = makeMockServer({ requireToken: 'good', adminToken: 'adm' });
  const mgr = new ConnectionManager({ fetchImpl, WebSocketImpl: StubWS });
  mgr.adoptExpired({ url: 'http://a:1', token: 'saved-old' });
  await assert.rejects(() => mgr.connect({ url: 'http://a:1', token: 'bad', saved: { token: 'saved-old' } }));
  assert.deepStrictEqual(mgr.snapshot(), [{ url: 'http://a:1', token: 'saved-old', expired: true }]);
});
