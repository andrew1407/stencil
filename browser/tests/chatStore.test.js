import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import {
  buildChatDoc, parseChatDoc, rowsToMessages, createChatStore, createIdbBackend,
  CHAT_MESSAGE_LIMIT, CHAT_DOC_VERSION,
} from '../js/llm/chatStore.js';

// Async Map-backed shim for the injected backend — the same injection idiom as
// projectsStore.test.js's localStorage shim, just promise-shaped like IndexedDB.
const makeBackend = (opts = {}) => {
  const m = new Map();
  return {
    _map: m,
    async get(k) { if (opts.throwOnGet) throw new Error('idb get'); return m.has(k) ? m.get(k) : undefined; },
    async set(k, v) { if (opts.throwOnSet) throw new Error('idb set'); m.set(k, v); },
    async remove(k) { m.delete(k); },
    async clear() { m.clear(); },
  };
};

test('buildChatDoc: text-only, role-filtered, trimmed to the §7 bound', () => {
  const doc = buildChatDoc([
    { role: 'user', text: 'crop it', images: [{ mediaType: 'image/png', data: 'zzz' }] },
    { role: 'assistant', text: 'Done.' },
    { role: 'system', text: 'never persisted' },
    { role: 'user', text: '' },            // empty text is not a turn
    { role: 'assistant' },                 // no text at all
  ], 1234);
  assert.deepStrictEqual(doc, {
    version: CHAT_DOC_VERSION,
    savedAt: 1234,
    messages: [{ role: 'user', text: 'crop it' }, { role: 'assistant', text: 'Done.' }],
  });
  // Images never survive — not even as an empty field (contract §12.1).
  assert.ok(!('images' in doc.messages[0]));

  const long = Array.from({ length: 40 }, (_, i) => ({ role: 'user', text: `t${i}` }));
  const trimmed = buildChatDoc(long, 0);
  assert.strictEqual(trimmed.messages.length, CHAT_MESSAGE_LIMIT);
  assert.strictEqual(trimmed.messages[0].text, 't8');   // the most recent 32 survive
});

test('parseChatDoc: accepts v1 (object or JSON string), strips junk, rejects the rest', () => {
  const doc = { version: 1, savedAt: 99, messages: [{ role: 'user', text: 'hi', images: ['x'] }] };
  assert.deepStrictEqual(parseChatDoc(doc), {
    version: 1, savedAt: 99, messages: [{ role: 'user', text: 'hi' }],
  });
  assert.deepStrictEqual(parseChatDoc(JSON.stringify(doc)).messages, [{ role: 'user', text: 'hi' }]);
  // Unknown version / malformed shapes are "no saved chat", never an error (§12.1).
  assert.strictEqual(parseChatDoc({ version: 2, messages: [] }), null);
  assert.strictEqual(parseChatDoc({ messages: [] }), null);
  assert.strictEqual(parseChatDoc({ version: 1, messages: 'nope' }), null);
  assert.strictEqual(parseChatDoc('not json'), null);
  assert.strictEqual(parseChatDoc(null), null);
  // Bad rows are dropped, good rows kept.
  const mixed = parseChatDoc({ version: 1, messages: [{ role: 'tool', text: 'x' }, { role: 'assistant', text: 'ok' }] });
  assert.deepStrictEqual(mixed.messages, [{ role: 'assistant', text: 'ok' }]);
});

test('rowsToMessages: settled user/assistant text only — no pending, errors, or cards', () => {
  assert.deepStrictEqual(rowsToMessages([
    { id: 1, role: 'user', text: 'make it sepia' },
    { id: 2, role: 'assistant', text: '…' },                      // in-flight
    { id: 3, role: 'assistant', text: 'Sepia applied.' },
    { id: 4, role: 'assistant', text: 'Error: boom', error: true },
    { id: 5, role: 'assistant', text: 'configure me', error: true, card: true },
  ]), [
    { role: 'user', text: 'make it sepia' },
    { role: 'assistant', text: 'Sepia applied.' },
  ]);
});

test('createChatStore: save/load/remove/clear round-trip over the injected backend', async () => {
  const backend = makeBackend();
  const store = createChatStore(backend);
  const doc = buildChatDoc([{ role: 'user', text: 'hello' }], 5);

  assert.strictEqual(await store.load('p_1'), null);
  assert.strictEqual(await store.save('p_1', doc), true);
  assert.deepStrictEqual(await store.load('p_1'), doc);

  await store.save('p_2', buildChatDoc([{ role: 'user', text: 'two' }], 6));
  await store.remove('p_1');
  assert.strictEqual(await store.load('p_1'), null);
  assert.notStrictEqual(await store.load('p_2'), null);

  await store.clear();
  assert.strictEqual(await store.load('p_2'), null);
});

test('createChatStore is best-effort: no backend and throwing backends degrade silently', async () => {
  const none = createChatStore(null);
  assert.strictEqual(await none.load('p_1'), null);
  assert.strictEqual(await none.save('p_1', buildChatDoc([], 0)), false);
  await none.remove('p_1');   // must not throw
  await none.clear();

  const broken = createChatStore(makeBackend({ throwOnGet: true, throwOnSet: true }));
  assert.strictEqual(await broken.load('p_1'), null);
  assert.strictEqual(await broken.save('p_1', buildChatDoc([{ role: 'user', text: 'x' }], 0)), false);

  // Corrupt stored bytes read as "no saved chat".
  const corrupt = makeBackend();
  corrupt._map.set('p_1', '{broken json');
  assert.strictEqual(await createChatStore(corrupt).load('p_1'), null);
});

test('createIdbBackend degrades to null without IndexedDB (Node)', () => {
  assert.strictEqual(createIdbBackend(null), null);
  assert.strictEqual(createIdbBackend(undefined ?? null), null);
});

// ── §12.1: the document is a CONVERSATION, never the machinery ──────────────
// The document is shared across surfaces (the .stencil file and the server's `chat`
// file kind), so a doc written by ANY surface can land here. The desktop was found
// building its copy from the MODEL's history, which carries §7's continuation note,
// the interim round-1 reply and the raw JSON plans. The browser has always written
// the DISPLAYED rows (audited: its document held only the two visible messages), but
// the filter now runs on both sides so neither direction can leak.
test('the §7 continuation note is never written and never restored', async () => {
  const { isInternalChatText, CONTINUATION_NOTE, buildChatDoc, parseChatDoc, rowsToMessages } =
    await import('../js/llm/chatStore.js');
  // The exact wire sentence, and any bracketed variant another surface might use.
  assert.strictEqual(isInternalChatText('user', CONTINUATION_NOTE), true);
  assert.strictEqual(isInternalChatText('user',
    '[The working image is now the frame you extracted — carry on.]'), true);
  assert.strictEqual(isInternalChatText('assistant', CONTINUATION_NOTE), true);
  // …and the controller pushes THAT constant, so the two can never drift.
  const ctrl = readFileSync(new URL('../js/llm/chatController.js', import.meta.url), 'utf8');
  assert.ok(ctrl.includes("import { CONTINUATION_NOTE } from './chatStore.js'"));
  assert.ok(ctrl.includes('const note = { role: \'user\', text: CONTINUATION_NOTE };'));
  assert.ok(!/text: '\[The working image is now/.test(ctrl), 'no second copy of the wording');
  // Refused on the way IN…
  const doc = buildChatDoc([
    { role: 'user', text: 'outline the cat' },
    { role: 'user', text: CONTINUATION_NOTE },
    { role: 'assistant', text: 'Outlined it.' },
  ]);
  assert.deepStrictEqual(doc.messages, [
    { role: 'user', text: 'outline the cat' },
    { role: 'assistant', text: 'Outlined it.' },
  ]);
  // …and on the way OUT, for a document some other build already wrote.
  const legacy = parseChatDoc({
    version: 1, savedAt: 5, messages: [
      { role: 'user', text: 'outline the cat' },
      { role: 'user', text: CONTINUATION_NOTE },
      { role: 'assistant', text: 'Outlined it.' },
    ],
  });
  assert.deepStrictEqual(legacy.messages, [
    { role: 'user', text: 'outline the cat' },
    { role: 'assistant', text: 'Outlined it.' },
  ]);
  // The transcript-row path shares the same gate.
  assert.deepStrictEqual(rowsToMessages([{ role: 'user', text: CONTINUATION_NOTE }]), []);
});

test('a raw op-plan replayed as an assistant turn never survives into the transcript', async () => {
  const { isInternalChatText, parseChatDoc } = await import('../js/llm/chatStore.js');
  const plan = '{"version":1,"reply":"Outlined it.","actions":[{"op":"layout","lines":[]}]}';
  // §7 permits the raw text on the WIRE; §12.1 forbids it in the document.
  assert.strictEqual(isInternalChatText('assistant', plan), true);
  assert.strictEqual(isInternalChatText('assistant', '{"version":1,"ask":{"question":"which?"}}'), true);
  // A USER may legitimately paste JSON and must see it again.
  assert.strictEqual(isInternalChatText('user', plan), false);
  // Ordinary replies are untouched, including ones that merely mention braces.
  assert.strictEqual(isInternalChatText('assistant', 'Use {"op":"crop"} to crop.'), false);
  assert.strictEqual(isInternalChatText('assistant', 'Outlined it.'), false);
  assert.strictEqual(isInternalChatText('assistant', ''), false);
  const doc = parseChatDoc({
    version: 1, savedAt: 1, messages: [
      { role: 'user', text: 'outline the cat' },
      { role: 'assistant', text: plan },
      { role: 'assistant', text: 'Outlined it.' },
    ],
  });
  assert.deepStrictEqual(doc.messages, [
    { role: 'user', text: 'outline the cat' },
    { role: 'assistant', text: 'Outlined it.' },
  ]);
});

test('a document this build writes round-trips identically', async () => {
  const { buildChatDoc, parseChatDoc, rowsToMessages, CHAT_MESSAGE_LIMIT } = await import('../js/llm/chatStore.js');
  // The rows a turn leaves behind, late chain note included (it lives IN the reply).
  const rows = [
    { id: 1, role: 'user', text: 'outline the face and the body' },
    { id: 2, role: 'assistant', text: 'Outlined both.\n(2 outline(s) could not be sharpened at higher zoom)' },
    { id: 3, role: 'assistant', text: 'Couldn\'t reach it', error: true, card: true },
    { id: 4, role: 'assistant', text: '…', pending: true },
  ];
  const doc = buildChatDoc(rowsToMessages(rows), 42);
  assert.deepStrictEqual(doc, {
    version: 1, savedAt: 42,
    messages: [
      { role: 'user', text: 'outline the face and the body' },
      { role: 'assistant', text: 'Outlined both.\n(2 outline(s) could not be sharpened at higher zoom)' },
    ],
  });
  // Through JSON (the server's `chat` file kind is bytes) and back, unchanged.
  const back = parseChatDoc(JSON.stringify(doc));
  assert.deepStrictEqual(back.messages, doc.messages);
  assert.strictEqual(back.version, 1);
  assert.strictEqual(back.savedAt, 42);
  assert.ok(doc.messages.length <= CHAT_MESSAGE_LIMIT);
});

test('sanitizeChatMessages is the same gate, for a doc handed over unparsed', async () => {
  const { sanitizeChatMessages, CONTINUATION_NOTE, CHAT_MESSAGE_LIMIT } = await import('../js/llm/chatStore.js');
  assert.deepStrictEqual(sanitizeChatMessages([
    { role: 'user', text: 'hi' },
    { role: 'user', text: CONTINUATION_NOTE },
    { role: 'assistant', text: '{"version":1,"reply":"x","actions":[]}' },
    { role: 'assistant', text: 'Done.', images: ['data:image/png;base64,AAA'] },
    { role: 'system', text: 'nope' },
    { role: 'assistant', text: '' },
    null,
  ]), [{ role: 'user', text: 'hi' }, { role: 'assistant', text: 'Done.' }]);
  // Images are never carried, and the §7 bound still applies.
  const many = Array.from({ length: 40 }, (_, i) => ({ role: 'user', text: `m${i}` }));
  const out = sanitizeChatMessages(many);
  assert.strictEqual(out.length, CHAT_MESSAGE_LIMIT);
  assert.strictEqual(out[0].text, 'm8');
  assert.deepStrictEqual(sanitizeChatMessages(null), []);
});
