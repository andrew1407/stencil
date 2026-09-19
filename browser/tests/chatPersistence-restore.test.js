// Restoring a persisted chat (js/llm/chatPersistence.js): removal cleanup, a silent server-push
// failure, the scripting surface, and what this build writes. From chatPersistence.test.js.
import { test, beforeEach } from 'node:test';
import assert from 'node:assert';

import { installMemoryStorage } from './helpers/memoryStorage.js';

const mem = installMemoryStorage()._map;

const { createChatPersistence, wireChatPersistence } = await import('../js/llm/chatPersistence.js');
const { createChatStore, buildChatDoc } = await import('../js/llm/chatStore.js');
const {
  appendChatRow, updateChatRow, clearChatLog, chatLog, resetChatLog, forgetChatController, peekChatController,
  clearSharedConversation,
} = await import('../js/llm/chatSession.js');

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
test('project removal / clear-all clean up stored chats regardless of the toggle', async () => {
  const store = makeStore();
  store._map.set('p_1', buildChatDoc([{ role: 'user', text: 'x' }], 1));
  store._map.set('p_2', buildChatDoc([{ role: 'user', text: 'y' }], 1));
  const app = makeApp();
  const chat = createChatPersistence({ app, store, getSettings: disabledSettings });
  await chat.projectRemoved('p_1');
  assert.ok(!store._map.has('p_1'));
  assert.ok(store._map.has('p_2'));
  await chat.allProjectsCleared();
  assert.strictEqual(store._map.size, 0);
  forgetChatController(app);
});

test('server push failures degrade silently (the local copy still lands)', async () => {
  const store = makeStore();
  conn = makeConn();
  conn.putFile = async () => { throw new Error('network down'); };
  const app = makeApp({
    activeId: 'p_1',
    meta: { p_1: { address: 'http://srv:8090', remoteId: 'r_9' } },
  });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings, now: () => 1 });
  appendChatRow({ role: 'user', text: 'hi' });
  await chat.flush();   // must not reject
  assert.ok(store._map.has('p_1'));
  forgetChatController(app);
});

// The panel publishes app.chat on stencil:ready, before index.js wires persistence —
// sharing the property silently broke stencil.prompt() / stencil.chat.*.
test('wiring persistence does not replace the panel scripting surface', async () => {
  const store = makeStore();
  const app = makeApp({ activeId: 'p_1' });
  // What chatPanel.js installs on stencil:ready.
  const panel = { open() {}, close() {}, isOpen: () => false, dock() {}, prompt: async () => ({}) };
  app.chat = panel;

  const chat = wireChatPersistence(app, { store, getSettings: disabledSettings });

  assert.strictEqual(app.chat, panel, 'the panel surface survives the persistence wiring');
  for (const m of ['open', 'close', 'isOpen', 'dock', 'prompt']) {
    assert.strictEqual(typeof app.chat[m], 'function', `stencil facade still reaches app.chat.${m}`);
  }
  assert.strictEqual(app.chatPersistence, chat, 'persistence lands on its own namespace');
  assert.strictEqual(typeof app.chatPersistence.projectOpened, 'function');
  forgetChatController(app);
});

// The §12 doc rides the .stencil file and the server's `chat` file kind, so a doc another surface built from
// its MODEL history must restore as a conversation — no continuation note, no raw plan — and seed the replay.
test('an old-style document with internal turns restores clean, both sides', async () => {
  const { CONTINUATION_NOTE } = await import('../js/llm/chatStore.js');
  const store = makeStore();
  // Exactly what a surface serialising its MODEL history produces (the desktop's bug):
  // the interim round-1 reply, the raw plans, and §7's continuation note.
  store._map.set('p_dirty', {
    version: 1,
    savedAt: 7,
    messages: [
      { role: 'user', text: 'make a page and outline the subject' },
      { role: 'assistant', text: '{"version":1,"reply":"Loading the page…","actions":[{"op":"blank","color":"#ffffff"}]}' },
      { role: 'user', text: CONTINUATION_NOTE },
      { role: 'assistant', text: '{"version":1,"reply":"Outlined the subject.","actions":[]}' },
      { role: 'assistant', text: 'Outlined the subject.' },
    ],
  });
  const app = makeApp({ activeId: 'p_dirty' });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings });
  await chat.projectOpened('p_dirty');
  await chat.flush();
  // The transcript reads as a conversation — the machinery is gone.
  assert.deepStrictEqual(chatLog().map((r) => ({ role: r.role, text: r.text })), [
    { role: 'user', text: 'make a page and outline the subject' },
    { role: 'assistant', text: 'Outlined the subject.' },
  ]);
  // …and so does the replay history seeded from it: the model is never handed its own
  // continuation note back as if the user had typed it.
  const ctrl = peekChatController(app);
  assert.deepStrictEqual(ctrl.history, [
    { role: 'user', text: 'make a page and outline the subject' },
    { role: 'assistant', text: 'Outlined the subject.' },
  ]);
  assert.ok(!JSON.stringify(ctrl.history).includes('The working image is now'));
  forgetChatController(app);
});

test('what THIS build writes contains only what the user saw', async () => {
  const store = makeStore();
  const app = makeApp({ activeId: 'p_clean' });
  const chat = createChatPersistence({ app, store, getSettings: enabledSettings, now: () => 9 });
  // A turn as the transcript ends up holding it: the user's prompt and the ONE reply,
  // with the background chain's late note already folded into that bubble.
  appendChatRow({ role: 'user', text: 'outline the subject' });
  const reply = appendChatRow({ role: 'assistant', text: '…', pending: true });
  updateChatRow(reply.id, { pending: false, text: 'Outlined the subject.' });
  updateChatRow(reply.id, { text: 'Outlined the subject.\n(2 outline(s) could not be sharpened)' });
  await chat.flush();
  assert.deepStrictEqual(store._map.get('p_clean'), {
    version: 1,
    savedAt: 9,
    messages: [
      { role: 'user', text: 'outline the subject' },
      { role: 'assistant', text: 'Outlined the subject.\n(2 outline(s) could not be sharpened)' },
    ],
  });
  forgetChatController(app);
});
