// applyLaunchQuery (js/console/launchQuery.js) over the facade rig — the servers, the assistant
// and the chat: a credential rides only the fragment, an unknown server asks first, a project
// locator connects then opens by id or name, and the assistant keys go through stencil.llm.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { boot, connected, notices, texts, called, lastCall, applyLaunchQuery } from '../helpers/launchQueryRig.js';

const SRV = 'https://srv.example';

test('connect= pairs its fragment token and asks before an unknown server; a refusal connects nothing', async () => {
  for (const answer of [true, false]) {
    const { app, stencil } = boot('', `#connect=${SRV}&token=abc`, {
      confirm(msg, opts) { this.calls.push(['confirm', msg, opts.title]); return Promise.resolve(answer); },
    }, connected(false));
    await applyLaunchQuery({ app, stencil });
    assert.deepEqual(lastCall(app, 'confirm'),
      ['confirm', `This link connects to ${SRV}. Connect to that server?`, 'Connect to server']);
    assert.deepEqual(called(app, 'connect'), answer ? [['connect', { url: SRV, token: 'abc' }]] : []);
    assert.deepEqual(notices, []);
  }
});

test('a server already connected is neither asked about nor reconnected', async () => {
  const { app, stencil } = boot(`?connect=${SRV}`, '', {
    confirm() { this.calls.push(['confirm']); return Promise.resolve(true); },
  }, connected(true));
  await applyLaunchQuery({ app, stencil });
  assert.equal(called(app, 'confirm').length, 0);
  assert.equal(called(app, 'connect').length, 0);
});

test('server=&id= opens the shared project through the server launch, incognito as an unlinked copy', async () => {
  for (const incognito of [false, true]) {
    const { app, stencil } = boot(`?server=${SRV}&id=r1&version=3${incognito ? '&incognito' : ''}`, '', {
      openRemoteProject(meta) { this.calls.push(['openRemoteProject', meta]); return Promise.resolve(); },
      copyServerProjectToIncognito(meta, opts) { this.calls.push(['copyServerProjectToIncognito', meta, opts]); return Promise.resolve(); },
    }, connected(true));
    await applyLaunchQuery({ app, stencil });
    const meta = { serverUrl: SRV, id: 'r1' };
    if (incognito) assert.deepEqual(lastCall(app, 'copyServerProjectToIncognito'), ['copyServerProjectToIncognito', meta, {}]);
    else assert.deepEqual(lastCall(app, 'openRemoteProject'), ['openRemoteProject', meta]);
    assert.deepEqual(notices, []);
  }
});

test('project= with a server locator connects (pairing its token), finds the project by id or name and opens it', async () => {
  const listing = [{ serverUrl: SRV, id: 'r1', name: 'Sketch', version: 4 }];
  for (const ref of ['r1', 'sketch']) {
    const { app, stencil } = boot('', `#project=${SRV}/projects/${ref}&connect=${SRV}&token=abc`, {
      confirm() { this.calls.push(['confirm']); return Promise.resolve(true); },
      openRemoteProject(meta) { this.calls.push(['openRemoteProject', meta]); return Promise.resolve(); },
    }, connected(false, listing));
    await applyLaunchQuery({ app, stencil });
    assert.deepEqual(called(app, 'connect'), [['connect', { url: SRV, token: 'abc' }]], ref);
    assert.deepEqual(lastCall(app, 'openRemoteProject'), ['openRemoteProject', { serverUrl: SRV, id: 'r1' }], ref);
    assert.deepEqual(notices, [], ref);
  }
});

test('project= without any server opens a local project by name, else by id, else is one notice', async () => {
  const named = boot('?project=Alpha', '', { activeProjectId: null, _metas: [{ id: 'p9', name: 'Alpha' }] });
  await applyLaunchQuery(named);
  assert.deepEqual(lastCall(named.app, 'switchToProject'), ['switchToProject', 'p9']);

  const byId = boot('?project=p7', '', { switchToProject(id) { this.calls.push(['switchToProject', id]); return true; } });
  await applyLaunchQuery(byId);
  assert.deepEqual(lastCall(byId.app, 'switchToProject'), ['switchToProject', 'p7']);

  const missing = boot('?project=nowhere', '', { switchToProject() { return false; } });
  await applyLaunchQuery(missing);
  assert.deepEqual(texts(), ['Launch link: picture — no local project "nowhere"']);
});

test('project= naming a project the server lacks is one notice', async () => {
  const { app, stencil } = boot(`?project=${SRV}/projects/ghost`, '', {}, connected(true, []));
  await applyLaunchQuery({ app, stencil });
  assert.deepEqual(texts(), [`Launch link: picture — no project "ghost" on ${SRV}`]);
});

test('the assistant settings go through stencil.llm.setup; a fragment key becomes this tab\'s session key', async () => {
  const { app, stencil } = boot('?llm-provider=ollama&llm-model=llava&llm-url=http://localhost:11434');
  await applyLaunchQuery({ app, stencil });
  assert.equal(stencil.llm.provider, 'ollama');
  assert.equal(stencil.llm.model, 'llava');
  assert.equal(stencil.llm.baseUrl, 'http://localhost:11434');
  assert.deepEqual(notices, []);

  const keyed = boot('', '#llm-provider=anthropic&llm-api-key=sk-test-1234');
  await applyLaunchQuery(keyed);
  assert.equal(keyed.stencil.llm.provider, 'anthropic');
  assert.equal(keyed.stencil.llm.apiKey, '[redacted]');
  assert.ok(keyed.stencil.llm.keyExpiresAt > Date.now());
  assert.equal(globalThis.localStorage.getItem('drawingApp_llmSettings').includes('sk-test'), false, 'never persisted');
  keyed.stencil.llm.forgetKey();
});

test('chat-dock= and chat=open reach the panel through the facade, dock first', async () => {
  const log = [];
  const { app, stencil } = boot('?chat=open&chat-dock=left', '', {
    chat: { open() { log.push('open'); }, dock(m) { log.push(`dock:${m}`); }, isOpen: () => false },
  });
  await applyLaunchQuery({ app, stencil });
  assert.deepEqual(log, ['dock:left', 'open']);
  assert.deepEqual(notices, []);
});
