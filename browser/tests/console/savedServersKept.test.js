// The persisted server set (js/console/stencilApi.js over net/connectionStore.js mergeSaved): a
// saved server that cannot be reached this session stays saved with its credential; only one the
// user disconnects leaves the list.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installMemoryStorage } from '../helpers/memoryStorage.js';
import { makeMockServer, StubWS } from '../helpers/connectionsRig.js';
import { mergeSaved, loadSavedServers, saveServers } from '../../js/net/connectionStore.js';

installMemoryStorage();

test('mergeSaved: the live set first, then saved servers neither live nor disconnected', () => {
  const live = [{ url: 'http://a:1', token: 'new-a' }];
  const saved = [{ url: 'http://a:1', token: 'old-a' }, { url: 'http://b:2', token: 'b' }, { url: 'http://c:3/', token: 'c' }];
  assert.deepEqual(mergeSaved(live, saved, ['http://c:3']), [
    { url: 'http://a:1', token: 'new-a' }, { url: 'http://b:2', token: 'b' }]);
});

test('a saved server that fails to connect is still saved; disconnecting it removes it', async () => {
  const up = makeMockServer();
  globalThis.WebSocket = StubWS;
  globalThis.fetch = (url, init) => (new URL(url).host === 'a:1'
    ? up.fetchImpl(url, init) : Promise.reject(new TypeError('connection refused')));
  localStorage.setItem('drawingApp_autoConnectServers', '0');
  saveServers([{ url: 'http://a:1', token: 'tok-a' }, { url: 'http://b:2', token: 'tok-b' }]);
  const { createStencil } = await import('../../js/console/stencilApi.js');
  const app = { lines: [], storage: { store: { list: () => [] }, incognito: false }, tabs: { onPeers() {} }, activeProjectId: null };
  const stencil = createStencil(app);

  await stencil.connect({ url: 'http://a:1', token: 'tok-a' });
  await stencil.connect({ url: 'http://b:2', token: 'tok-b' }).catch(() => {});
  assert.deepEqual(stencil.connections, ['http://a:1']);
  assert.deepEqual(loadSavedServers().map((s) => [s.url, s.token]), [['http://a:1', 'tok-a'], ['http://b:2', 'tok-b']],
    'the unreachable server keeps its saved row and credential');

  stencil.disconnect('http://b:2');
  assert.deepEqual(loadSavedServers().map((s) => s.url), ['http://a:1'], 'a disconnect is what forgets it');
  stencil.disconnect('http://a:1');
  assert.deepEqual(loadSavedServers(), []);
});
