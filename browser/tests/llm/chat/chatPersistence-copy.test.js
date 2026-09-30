// projectCopied (js/llm/chat/persistence.js): a whole-project copy takes its source's saved chat
// to a local id or a server `chat` file, whatever the saving toggle says.
import { test } from 'node:test';
import assert from 'node:assert';
import { installMemoryStorage } from '../../helpers/memoryStorage.js';

installMemoryStorage();
const { createChatPersistence } = await import('../../../js/llm/chat/persistence.js');
const { buildChatDoc } = await import('../../../js/llm/chat/store.js');

const DOC = buildChatDoc([{ role: 'user', text: 'make it sepia' }, { role: 'assistant', text: 'done' }], 5);

const makeStore = (seed = {}) => {
  const m = new Map(Object.entries(seed));
  return { _map: m, async load(id) { return m.get(String(id)) || null; },
    async save(id, doc) { m.set(String(id), doc); return true; }, async remove() {}, async clear() {} };
};
const makeConn = (doc = null) => {
  const conn = { puts: [],
    putFile: async (id, kind, body) => { conn.puts.push({ id, kind, body }); },
    fetchFile: async () => { if (!doc) throw new Error('HTTP 404'); return { text: async () => JSON.stringify(doc) }; } };
  return conn;
};
const persistence = (store) => createChatPersistence({
  app: { storage: { activeId: null, temporary: true, store: { getMeta: () => null } } },
  store, getSettings: () => ({ saveChats: false }),
});

test('a local chat is copied to the new local id, the toggle notwithstanding', async () => {
  const store = makeStore({ p1: DOC });
  assert.strictEqual(await persistence(store).projectCopied({ id: 'p1' }, { id: 'n1' }), true);
  assert.deepStrictEqual(store._map.get('n1').messages, DOC.messages);
});

test('a server chat file is copied to another server project', async () => {
  const from = makeConn(DOC);
  const to = makeConn();
  assert.strictEqual(await persistence(makeStore()).projectCopied({ conn: from, remoteId: 'r1' }, { conn: to, remoteId: 'r9' }), true);
  assert.deepStrictEqual([to.puts[0].id, to.puts[0].kind], ['r9', 'chat']);
  assert.deepStrictEqual(JSON.parse(to.puts[0].body).messages, DOC.messages);
});

test('no saved chat copies nothing', async () => {
  const store = makeStore();
  assert.strictEqual(await persistence(store).projectCopied({ id: 'p1' }, { id: 'n1' }), false);
  assert.strictEqual(await persistence(store).projectCopied({ conn: makeConn(), remoteId: 'r1' }, { id: 'n1' }), false);
  assert.strictEqual(store._map.size, 0);
});
