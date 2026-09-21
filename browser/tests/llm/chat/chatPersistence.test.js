import { test, beforeEach } from 'node:test';
import assert from 'node:assert';

// Install localStorage BEFORE the imports (llmSettings/connectionStore read it).
import { installMemoryStorage } from '../../helpers/memoryStorage.js';

const mem = installMemoryStorage()._map;

const { createChatPersistence, wireChatPersistence } = await import('../../../js/llm/chat/persistence.js');
const { createChatStore, buildChatDoc } = await import('../../../js/llm/chat/store.js');
const {
  appendChatRow, updateChatRow, clearChatLog, chatLog, resetChatLog, forgetChatController, peekChatController,
  clearSharedConversation,
} = await import('../../../js/llm/chat/session.js');

// Recording chat store over an async Map — counts calls so tests can assert the
// muted-restore rule (a restore must not write back what it just read).
const makeStore = () => {
  const m = new Map();
  const calls = { save: [], remove: [], clear: 0 };
  return {
    _map: m,
    calls,
    async load(id) { return m.get(String(id)) || null; },
    async save(id, doc) { calls.save.push(String(id)); m.set(String(id), doc); return true; },
    async remove(id) { calls.remove.push(String(id)); m.delete(String(id)); },
    async clear() { calls.clear++; m.clear(); },
  };
};

// A mock connected server: records file calls, serves a canned chat doc.
const makeConn = (chatDocJson = null) => ({
  putFile: async (id, kind, bytes, opts) => { conn.puts.push({ id, kind, bytes, opts }); },
  deleteFile: async (id, kind) => { conn.deletes.push({ id, kind }); },
  fetchFile: async (id, kind) => {
    conn.fetches.push({ id, kind });
    if (chatDocJson == null) { const e = new Error('HTTP 404'); e.status = 404; throw e; }
    return { text: async () => chatDocJson };
  },
  puts: [], deletes: [], fetches: [],
});
let conn;

const makeApp = ({ activeId = 'p_a', temporary = false, incognito = false, meta = {} } = {}) => ({
  storage: {
    activeId, temporary, incognito,
    store: { getMeta: (id) => meta[id] || null },
  },
  connections: { get: (url) => (conn && url === 'http://srv:8090' ? conn : null) },
});

const enabledSettings = () => ({ saveChats: true });
const disabledSettings = () => ({ saveChats: false });

beforeEach(() => {
  resetChatLog();
  conn = makeConn();
});

test('disabled (the default): turns never touch the store', async () => {
  const store = makeStore();
  const app = makeApp();
  const chat = createChatPersistence({ app, store, getSettings: disabledSettings, now: () => 1 });
  appendChatRow({ role: 'user', text: 'hello' });
  appendChatRow({ role: 'assistant', text: 'hi!' });
  await chat.flush();
  assert.deepStrictEqual(store.calls.save, []);
  forgetChatController(app);
});

test('enabled: settled turns persist under the active project, text-only', async () => {
  const store = makeStore();
  const app = makeApp({ activeId: 'p_1' });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings, now: () => 7 });
  appendChatRow({ role: 'user', text: 'make it sepia' });
  const pending = appendChatRow({ role: 'assistant', text: '…' });
  updateChatRow(pending.id, { text: 'Sepia applied.' });
  await chat.flush();
  assert.deepStrictEqual(store._map.get('p_1'), {
    version: 1, savedAt: 7,
    messages: [{ role: 'user', text: 'make it sepia' }, { role: 'assistant', text: 'Sepia applied.' }],
  });
  forgetChatController(app);
});

test('temporary and incognito editors never persist, even when enabled', async () => {
  for (const overrides of [{ temporary: true }, { incognito: true }, { activeId: null }]) {
    resetChatLog();
    const store = makeStore();
    const app = makeApp(overrides);
    const chat = createChatPersistence({ app, store, getSettings: enabledSettings });
    appendChatRow({ role: 'user', text: 'psst' });
    await chat.flush();
    assert.deepStrictEqual(store.calls.save, [], JSON.stringify(overrides));
    forgetChatController(app);
  }
});

test('a project switch mid-turn cannot file the chat under the new project', async () => {
  const store = makeStore();
  const app = makeApp({ activeId: 'p_old' });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings, now: () => 1 });
  appendChatRow({ role: 'user', text: 'belongs to p_old' });   // snapshot keyed NOW
  app.storage.activeId = 'p_new';                              // switch before the write lands
  await chat.flush();
  assert.ok(store._map.has('p_old'), 'written under the id captured at snapshot time');
  assert.ok(!store._map.has('p_new'));
  forgetChatController(app);
});

test('clearing the conversation deletes the persisted copy (local + linked server)', async () => {
  const store = makeStore();
  const app = makeApp({
    activeId: 'p_1',
    meta: { p_1: { address: 'http://srv:8090', remoteId: 'r_9' } },
  });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings, now: () => 1 });
  appendChatRow({ role: 'user', text: 'hi' });
  await chat.flush();
  assert.ok(store._map.has('p_1'));
  assert.strictEqual(conn.puts.length, 1);
  assert.deepStrictEqual({ kind: conn.puts[0].kind, ext: conn.puts[0].opts.ext }, { kind: 'chat', ext: 'json' });

  clearChatLog();   // the panel's Clear empties the shared log
  await chat.flush();
  assert.ok(!store._map.has('p_1'), 'local copy deleted');
  assert.deepStrictEqual(conn.deletes, [{ id: 'r_9', kind: 'chat' }], 'server copy deleted');
  forgetChatController(app);
});

test('the shared clear path (clearChat op / stencil.chat.clear) wipes the persisted copy too', async () => {
  const store = makeStore();
  const app = makeApp({ activeId: 'p_1' });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings, now: () => 1 });
  appendChatRow({ role: 'user', text: 'hi' });
  await chat.flush();
  assert.ok(store._map.has('p_1'));

  clearSharedConversation(app);   // the trash button / facade / §10 op all route here
  await chat.flush();
  assert.strictEqual(chatLog().length, 0);
  assert.ok(!store._map.has('p_1'), 'the §12 copy goes with the emptied transcript');
  forgetChatController(app);
});

test('projectOpened restores rows + controller history without writing back', async () => {
  const store = makeStore();
  const saved = buildChatDoc([{ role: 'user', text: 'old turn' }, { role: 'assistant', text: 'old reply' }], 3);
  store._map.set('p_2', saved);
  const app = makeApp({ activeId: 'p_2' });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings });
  await chat.projectOpened('p_2');
  await chat.flush();
  assert.deepStrictEqual(chatLog().map((r) => ({ role: r.role, text: r.text })), [
    { role: 'user', text: 'old turn' }, { role: 'assistant', text: 'old reply' },
  ]);
  const ctrl = peekChatController(app);
  assert.deepStrictEqual(ctrl.history, [
    { role: 'user', text: 'old turn' }, { role: 'assistant', text: 'old reply' },
  ]);
  assert.deepStrictEqual(store.calls.save, [], 'a restore never writes back');
  forgetChatController(app);
});

test('projectOpened falls back to the server chat file when nothing is stored locally', async () => {
  const store = makeStore();
  conn = makeConn(JSON.stringify(buildChatDoc([{ role: 'user', text: 'from server' }], 5)));
  const app = makeApp({
    activeId: 'p_srv',
    meta: { p_srv: { address: 'http://srv:8090', remoteId: 'r_1' } },
  });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings });
  await chat.projectOpened('p_srv');
  assert.deepStrictEqual(conn.fetches, [{ id: 'r_1', kind: 'chat' }]);
  assert.deepStrictEqual(chatLog().map((r) => r.text), ['from server']);
  forgetChatController(app);
});

test('projectOpened(null) resets the scope; disabled leaves the session log alone', async () => {
  const store = makeStore();
  const app = makeApp({ activeId: 'p_1' });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings });
  appendChatRow({ role: 'user', text: 'x' });
  await chat.projectOpened(null);
  assert.strictEqual(chatLog().length, 0, 'fresh scope for a temporary editor');

  appendChatRow({ role: 'user', text: 'kept' });
  const off = createChatPersistence({ app, store, getSettings: disabledSettings });
  await off.projectOpened('p_1');
  assert.strictEqual(chatLog().length, 1, 'with saving off the conversation survives switches');
  forgetChatController(app);
});
