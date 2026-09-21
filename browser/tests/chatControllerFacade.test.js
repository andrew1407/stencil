// The window.stencil chat facade (js/console/stencilApi.js over the real controller): llm
// settings, prompt(), the frozen members, image adoption and the retry re-queue.
import { test } from 'node:test';
import assert from 'node:assert';
import { splitDataUrl } from '../js/llm/chat/chatController.js';
import { installMemoryStorage } from './helpers/memoryStorage.js';
import {
  makeClient, chatOnlyReply, variantPlan, pngUrl, makeController,
} from './helpers/chatControllerRig.js';

// ── window.stencil facade: llm settings + prompt() + chat panel control ──
// A localStorage so the llm settings store persists.
const mem = installMemoryStorage()._map;
const { createStencil } = await import('../js/console/stencilApi.js');
const { ConnectionManager } = await import('../js/net/connectionManager.js');

// The facade only needs what createStencil touches at construction, plus app.chat —
// the surface the real panel registers on wire; here it is glued to a REAL controller.
const facadeApp = (client) => {
  const { controller } = makeController(client);
  const state = { open: false, dock: 'left' };
  const app = {
    connections: new ConnectionManager({ fetchImpl: async () => ({ ok: true, json: async () => ({}) }) }),
    lines: [], storage: { store: { list: () => [] }, incognito: false },
    tabs: { onPeers() {} },
    activeProjectId: null,
  };
  app.chat = {
    open: () => { state.open = true; },
    close: () => { state.open = false; },
    isOpen: () => state.open,
    dock: (mode) => {
      if (!['left', 'right', 'top', 'bottom', 'float'].includes(mode)) throw new Error(`Unknown dock mode "${mode}"`);
      state.dock = mode;
    },
    prompt: async (text, images = []) => {
      for (const u of images) controller.addImageDataUrl(u);
      return controller.send(text);
    },
    clear: () => { state.cleared = (state.cleared || 0) + 1; },
    controller: () => controller,
  };
  return { app, controller, state };
};

test('stencil.llm: settings round-trip through the shared store; validation throws', () => {
  mem.clear();
  const { app } = facadeApp(makeClient(chatOnlyReply));
  const stencil = createStencil(app);

  assert.strictEqual(stencil.llm.provider, 'none');                         // contract default
  stencil.llm.setup({ provider: 'openai-compat', model: 'qwen-vl', apiKey: 'sk-1' });
  assert.strictEqual(stencil.llm.provider, 'openai-compat');
  assert.strictEqual(stencil.llm.baseUrl, 'http://localhost:1234/v1');      // default refilled on switch
  assert.strictEqual(stencil.llm.model, 'qwen-vl');
  assert.strictEqual(stencil.llm.apiKey, 'sk-1');                           // read back as-is (token precedent)
  // The persisted JSON is the same drawingApp_llmSettings the modal uses.
  assert.strictEqual(JSON.parse(mem.get('drawingApp_llmSettings')).model, 'qwen-vl');

  stencil.llm.baseUrl = 'http://box:9999/v1';                               // property-style setter
  assert.strictEqual(stencil.llm.baseUrl, 'http://box:9999/v1');
  stencil.llm.setup({ provider: 'ollama' });                                // overridden URL survives the switch
  assert.strictEqual(stencil.llm.baseUrl, 'http://box:9999/v1');

  assert.throws(() => stencil.llm.setup({ provider: 'weird' }), /Unknown LLM provider/);
  assert.throws(() => { stencil.llm.provider = 'weird'; }, /Unknown LLM provider/);
  assert.throws(() => stencil.llm.setup({ baseUrl: 'not a url' }), /http\(s\) URL/);
  assert.throws(() => stencil.llm.setup({ serverUrl: 'ftp://x' }), /http\(s\) URL/);
});

test('stencil.prompt runs the panel pipeline end-to-end (shared history, images attach)', async () => {
  mem.clear();
  const client = makeClient(variantPlan);
  const { app, controller } = facadeApp(client);
  const stencil = createStencil(app);

  const entry = await stencil.prompt('two variants please', { images: [pngUrl('CONSOLE')] });
  assert.strictEqual(entry.reply, 'Two takes for you.');
  assert.strictEqual(entry.results.length, 2);
  assert.deepStrictEqual(entry.results.map((r) => r.label), ['sepia', 'cropped']);
  // The scripted image rode the turn behind the working-image snapshot, and the
  // exchange lives in the SHARED history.
  assert.deepStrictEqual(client.calls[0].messages[0].images,
    [splitDataUrl(pngUrl('SHOT')), splitDataUrl(pngUrl('CONSOLE'))]);
  assert.strictEqual(controller.history.length, 2);
  assert.strictEqual(controller.history[0].text, 'two variants please');
});

test('stencil.prompt rejects with the typed client errors; chat controls chain', async () => {
  mem.clear();
  const err = Object.assign(new Error('Response truncated'), { kind: 'truncated' });
  const { app, state } = facadeApp({ calls: [], chat: async () => { throw err; } });
  const stencil = createStencil(app);
  await assert.rejects(() => stencil.prompt('hi'), /truncated/);

  assert.strictEqual(stencil.chat.isOpen, false);
  assert.strictEqual(stencil.chat.open(), stencil);
  assert.strictEqual(stencil.chat.isOpen, true);
  assert.strictEqual(stencil.chat.dock('bottom'), stencil);
  assert.throws(() => stencil.chat.dock('diagonal'), /Unknown dock mode/);
  // clear() routes to the panel's trash-button path and chains like the rest.
  assert.strictEqual(stencil.chat.clear(), stencil);
  assert.strictEqual(state.cleared, 1);
  assert.strictEqual(stencil.chat.close(), stencil);
  assert.strictEqual(stencil.chat.isOpen, false);
});

test('facade guard: llm/prompt/chat members are frozen like the rest', () => {
  mem.clear();
  const { app } = facadeApp(makeClient(chatOnlyReply));
  const stencil = createStencil(app);
  assert.throws(() => { stencil.prompt = 0; }, /read-only/);
  assert.throws(() => { stencil.llm.setup = 0; }, /read-only/);
  assert.throws(() => { stencil.chat.open = 0; }, /read-only/);
  // Settings setters still write through the guard (they are real accessors).
  stencil.llm.model = 'still-writable';
  assert.strictEqual(stencil.llm.model, 'still-writable');
});

// A picture the prompt asks you to ACT ON becomes the working image; otherwise the only answer is
// "the canvas is empty" about an image sitting right there in the message.
test('an editing plan on an empty editor adopts the attached image', async () => {
  const loaded = [];
  const { controller } = makeController(
    makeClient([JSON.stringify({ version: 1, reply: 'Converted.', actions: [{ op: 'filter', mode: 'bw' }], variants: [] })]),
    {
      // An EMPTY editor: no working-image snapshot for this turn.
      workingSnapshot: async () => null,
      loadImage: async (dataUrl, name) => { loaded.push(name); },
    });
  controller.addImageDataUrl(pngUrl('CAT'), 'cat.png');
  const res = await controller.send('make it black and white');
  assert.deepEqual(loaded, ['cat.png'], 'the attachment became the working image');
  assert.ok(res.warnings.some((w) => /opened cat\.png in the editor first/.test(w)),
    'and the reply says so, so the edit is not a silent surprise');
});

test('a chat-only question about an attachment leaves the editor alone', async () => {
  const loaded = [];
  const { controller } = makeController(
    makeClient([JSON.stringify({ version: 1, reply: 'A tabby cat.', actions: [], variants: [] })]),
    { workingSnapshot: async () => null, loadImage: async (d, n) => { loaded.push(n); } });
  controller.addImageDataUrl(pngUrl('CAT'), 'cat.png');
  await controller.send('what is this?');
  assert.deepEqual(loaded, [], 'nothing was opened — the question only needed to look');
});

test('with an image already open, an attachment stays a reference', async () => {
  const loaded = [];
  const { controller } = makeController(
    makeClient([JSON.stringify({ version: 1, reply: 'Done.', actions: [{ op: 'filter', mode: 'bw' }], variants: [] })]),
    { workingSnapshot: async () => pngUrl('SHOT'), loadImage: async (d, n) => { loaded.push(n); } });
  controller.addImageDataUrl(pngUrl('REF'), 'reference.png');
  await controller.send('make it black and white');
  assert.deepEqual(loaded, [], 'the open image is what gets edited, not the reference');
});

test('retry re-queues the failed turn\'s attachments so the resend carries them', async () => {
  let fail = true;
  const calls = [];
  const client = {
    chat: async ({ messages }) => {
      calls.push(messages);
      if (fail) { fail = false; throw new Error('network down'); }
      return chatOnlyReply;
    },
  };
  const { controller } = makeController(client);
  controller.addImageDataUrl(pngUrl('CAT'), 'cat.png');
  await assert.rejects(() => controller.send('highlight the cat'), /network down/);
  assert.strictEqual(controller.attachments.length, 0, 'the send drained the queue');
  controller.requeueLastTurnAttachments();
  assert.strictEqual(controller.attachments.length, 1, 'retry restores the drained attachment');
  assert.strictEqual(controller.attachments[0].name, 'cat.png');
  await controller.send('highlight the cat');
  const last = calls.at(-1).at(-1);
  assert.ok((last.images || []).some((i) => i.data === 'CAT'), 'the resend carries the image');
  // Something newly queued is never clobbered by a later requeue.
  controller.addImageDataUrl(pngUrl('DOG'), 'dog.png');
  controller.requeueLastTurnAttachments();
  assert.deepStrictEqual(controller.attachments.map((a) => a.name), ['dog.png']);
});
