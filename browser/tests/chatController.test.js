import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { createChatController, replayMessages, splitDataUrl, HISTORY_LIMIT, EDGE_MAP_SENTENCE, MAX_IMAGE_EDGE } from '../js/llm/chatController.js';
import PROMPT_ASSET from '../js/config/llm/systemPrompt.json' with { type: 'json' };
import { installMemoryStorage } from './helpers/memoryStorage.js';

// ── Doubles: client capturing what it was sent, stub facade recording calls ──
const makeClient = (replies) => {
  const calls = [];
  const queue = Array.isArray(replies) ? replies.slice() : [replies];
  return {
    calls,
    chat: async ({ system, messages }) => {
      calls.push({ system, messages });
      return queue.length > 1 ? queue.shift() : queue[0];
    },
  };
};

const makeStencil = () => {
  const calls = [];
  const stencil = {
    imageSize: { width: 800, height: 600 },
    connections: [],
    crop(spec) { calls.push(['crop', spec]); return stencil; },
    rotateLeft() { calls.push(['rotateLeft']); return stencil; },
    rotateRight() { calls.push(['rotateRight']); return stencil; },
    apply(opts) { calls.push(['apply', opts]); return stencil; },
    async blank(color) { calls.push(['blank', color]); return stencil; },
    async load(url, opts) { calls.push(['load', url, opts]); return stencil; },
    async connect(entry) { calls.push(['connect', entry]); return stencil; },
    disconnect(url) { calls.push(['disconnect', url]); return stencil; },
    undo() { calls.push(['undo']); return stencil; },
    redo() { calls.push(['redo']); return stencil; },
    zoomFit() { calls.push(['zoomFit']); return stencil; },
  };
  Object.defineProperty(stencil, 'darkTheme', { set(v) { calls.push(['darkTheme', v]); } });
  Object.defineProperty(stencil, 'mainTheme', { set(v) { calls.push(['mainTheme', v]); } });
  Object.defineProperty(stencil, 'zoomLevel', { set(v) { calls.push(['zoomLevel', v]); } });
  Object.defineProperty(stencil, 'compareMode', { set(v) { calls.push(['compareMode', v]); } });
  Object.defineProperty(stencil, 'compareSplit', { set(v) { calls.push(['compareSplit', v]); } });
  // The facade's incognito rule: only togglable on a blank editor — throws otherwise.
  Object.defineProperty(stencil, 'incognito', { set(v) {
    if (v && stencil.imageSize) throw new Error('Incognito can only be enabled on a blank editor (before an image is loaded)');
    calls.push(['incognitoSet', v]);
  } });
  return { stencil, calls };
};

const chatOnlyReply = 'Just chatting, no JSON.';
const variantPlan = JSON.stringify({
  version: 1,
  reply: 'Two takes for you.',
  actions: [{ op: 'rotate', dir: 'left' }],
  variants: [
    { label: 'sepia', actions: [{ op: 'filter', mode: 'sepia' }] },
    { label: 'cropped', actions: [{ op: 'crop', spec: { x1: '10%' } }] },
  ],
});

const pngUrl = (data) => `data:image/png;base64,${data}`;
const stubFile = (name, type) => ({ name, type });

const makeController = (client, over = {}) => {
  const { stencil, calls } = makeStencil();
  let exports = 0;
  const exported = [];
  const controller = createChatController({
    stencil,
    getClient: () => client,
    exportImage: async () => { const u = pngUrl(`EXP${exports++}`); exported.push(u); return u; },
    prepareAttachment: async (file) => pngUrl(`IMG_${file.name}`),
    extractFrames: async (file, n) => Array.from({ length: n }, (_, i) => pngUrl(`FR${i}_${file.name}`)),
    frameAt: async (file, i) => pngUrl(`AT${i}_${file.name}`),
    // Contract §7: every turn carries a snapshot of the working image. Injected here
    // so it is distinguishable from the variant exports (and needs no DOM downscale).
    workingSnapshot: async () => pngUrl('SHOT'),
    ...over,
  });
  return { controller, stencil, calls, exported };
};

test('splitDataUrl splits a data URL into the LlmImage wire shape', () => {
  assert.deepStrictEqual(splitDataUrl('data:image/png;base64,AAAA'), { mediaType: 'image/png', data: 'AAAA' });
  assert.strictEqual(splitDataUrl('http://x/y.png'), null);
});

test('a 2-variant plan → 2 exports (no base snapshot), thumbnails returned', async () => {
  const client = makeClient(variantPlan);
  const { controller, calls, exported } = makeController(client);

  const entry = await controller.send('make variants');
  assert.strictEqual(entry.reply, 'Two takes for you.');
  assert.strictEqual(entry.chatOnly, false);
  assert.deepStrictEqual(entry.warnings, []);
  // One export per variant — no base snapshot: crop/rotate variants are undone on the model.
  assert.deepStrictEqual(exported, [pngUrl('EXP0'), pngUrl('EXP1')]);
  assert.deepStrictEqual(entry.results, [
    { label: 'sepia', dataUrl: pngUrl('EXP0') },
    { label: 'cropped', dataUrl: pngUrl('EXP1') },
  ]);
  // The top-level action and each variant's own op run exactly once; the sepia variant is
  // undone by restoring settings (the stub's crop never moves the rect — nothing to re-commit).
  assert.equal(calls.filter((c) => c[0] === 'rotateLeft').length, 1);
  assert.equal(calls.filter((c) => c[0] === 'crop').length, 1);
  assert.ok(calls.some((c) => c[0] === 'apply' && c[1].filter === 'sepia'));
  assert.deepStrictEqual(calls.filter((c) => c[0] === 'load'), []);
});

test('history keeps the wire shape: user turn + RAW assistant text, chat-only turns too', async () => {
  const client = makeClient([variantPlan, chatOnlyReply]);
  const { controller } = makeController(client);

  await controller.send('make variants');
  assert.deepStrictEqual(controller.history, [
    // The user turn carries the auto-attached working image (§7).
    { role: 'user', text: 'make variants', images: [splitDataUrl(pngUrl('SHOT'))] },
    { role: 'assistant', text: variantPlan },   // raw model text, not the parsed reply
  ]);

  const entry = await controller.send('and how are you?');
  assert.strictEqual(entry.chatOnly, true);
  assert.strictEqual(entry.reply, chatOnlyReply);
  assert.strictEqual(controller.history.length, 4);
  assert.deepStrictEqual(controller.history[3], { role: 'assistant', text: chatOnlyReply });
});

test('system prompt carries the dynamic working-image suffix after the verbatim prompt', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);
  await controller.send('hi');
  assert.ok(client.calls[0].system.startsWith(PROMPT_ASSET.head));   // verbatim asset head, nothing prepended
  assert.ok(client.calls[0].system.includes('Current working image: 800x600 px.'));
});

test('system prompt appends the §10 editor-settings block inside the op list (browser profile)', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);
  await controller.send('hi');
  const sys = client.calls[0].system;
  assert.ok(sys.includes('- {"op":"theme","mode":"light"|"dark"} — switch the editor between light and dark'));
  assert.ok(sys.includes('never invent or suggest a new address'));
  // Block sits after the frame line and before the chat-only paragraph.
  assert.ok(sys.indexOf('{"op":"frame"') < sys.indexOf('{"op":"theme"'));
  assert.ok(sys.indexOf('{"op":"theme"') < sys.indexOf('If the user is only chatting'));
});

test('editor-settings ops dispatch through the facade; connect resolves the injected saved servers', async () => {
  const settingsPlan = JSON.stringify({
    version: 1,
    reply: 'done',
    actions: [
      { op: 'theme', mode: 'dark' },
      { op: 'view', lines: false },
      { op: 'connect', server: 'alpha' },
    ],
  });
  const saved = [{ url: 'http://alpha:8090', token: 'stored-tok' }];
  const { controller, calls } = makeController(makeClient(settingsPlan), { savedServers: () => saved });
  const entry = await controller.send('dark mode, hide lines, connect alpha');
  assert.strictEqual(entry.reply, 'done');
  assert.deepStrictEqual(calls, [
    ['darkTheme', true],
    ['apply', { showLines: false }],
    ['connect', saved[0]],          // the SAVED entry — its stored token, never a plan token
  ]);
});

test('clearChat rides a turn end-to-end: deferred past the other actions, notes surface', async () => {
  const clearPlan = JSON.stringify({
    version: 1,
    reply: 'clearing',
    actions: [{ op: 'clearChat' }, { op: 'theme', mode: 'dark' }],
  });
  const seen = [];
  const { controller, calls } = makeController(makeClient(clearPlan), {
    clearChatConversation: async () => { seen.push(calls.map((c) => c[0])); return null; },
  });
  const entry = await controller.send('dark mode, then wipe this chat');
  assert.deepStrictEqual(entry.warnings, []);
  // The confirm/clear happened once, AFTER the theme action that followed it.
  assert.deepStrictEqual(seen, [['darkTheme']]);

  // Declined: the "clear canceled" note comes back as a warning, never a failure.
  const { controller: c2 } = makeController(makeClient(clearPlan), {
    clearChatConversation: async () => 'clear canceled',
  });
  const declined = await c2.send('wipe it');
  assert.ok(declined.warnings.some((w) => w.includes('clearChat: clear canceled')));
});

test('clearChat defers past the §7 auto-continuation — the confirm fires after round two', async () => {
  const loadPlan = JSON.stringify({
    version: 1, reply: 'loading', actions: [{ op: 'blank', color: '#ffffff' }, { op: 'clearChat' }],
  });
  const followUp = JSON.stringify({ version: 1, reply: 'done', actions: [{ op: 'theme', mode: 'dark' }] });
  const client = makeClient([loadPlan, followUp]);
  const seen = [];
  const { controller, calls } = makeController(client, {
    clearChatConversation: async () => { seen.push({ rounds: client.calls.length, ran: calls.map((c) => c[0]) }); return null; },
  });
  const entry = await controller.send('blank page, then wipe this chat');
  assert.strictEqual(client.calls.length, 2, 'the load auto-continued into a second round');
  assert.deepStrictEqual(seen.map((s) => s.rounds), [2], 'one confirm, only after round two went out');
  assert.ok(seen[0].ran.includes('darkTheme'), 'the continuation plan executed before the clear');
  assert.strictEqual(entry.reply, 'done');
});

test('a clearChat asked by BOTH rounds confirms once, at the very end of the turn', async () => {
  const loadPlan = JSON.stringify({
    version: 1, reply: 'loading', actions: [{ op: 'blank', color: '#ffffff' }, { op: 'clearChat' }],
  });
  const followUp = JSON.stringify({ version: 1, reply: 'done', actions: [{ op: 'clearChat' }] });
  const client = makeClient([loadPlan, followUp]);
  let confirms = 0;
  const { controller } = makeController(client, {
    clearChatConversation: async () => { confirms++; return null; },
  });
  const entry = await controller.send('new blank, wipe the chat');
  assert.strictEqual(client.calls.length, 2);
  assert.strictEqual(confirms, 1, 'deduped — the turn shows a single confirm');
  assert.deepStrictEqual(entry.warnings, []);
});

test('connect to an unsaved host rejects with the unknown-server plan error, nothing executed', async () => {
  const planTxt = JSON.stringify({ version: 1, reply: 'x', actions: [{ op: 'connect', server: 'http://evil:1' }] });
  const { controller, calls } = makeController(makeClient(planTxt), {
    savedServers: () => [{ url: 'http://alpha:8090', token: 't' }],
  });
  await assert.rejects(() => controller.send('connect somewhere new'), /Unknown server/);
  assert.deepStrictEqual(calls, []);
});

test('disconnect resolves against the facade\'s live connections', async () => {
  const planTxt = JSON.stringify({ version: 1, reply: 'x', actions: [{ op: 'disconnect', server: 'beta' }] });
  const client = makeClient(planTxt);
  const { stencil, calls } = makeStencil();
  stencil.connections = ['http://alpha:8090', 'https://beta:9090'];
  const controller = createChatController({ stencil, getClient: () => client, savedServers: () => [] });
  await controller.send('drop beta');
  assert.deepStrictEqual(calls, [['disconnect', 'https://beta:9090']]);
});

test('image replay rule: current turn images + the single most recent prior image', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);

  // Turn 1: two attached images.
  await controller.addAttachment(stubFile('a.png', 'image/png'));
  await controller.addAttachment(stubFile('b.png', 'image/png'));
  await controller.send('look at these');
  // Current turn keeps ALL its images — the working-image snapshot leads, the user's
  // own attachments follow in order.
  assert.deepStrictEqual(client.calls[0].messages[0].images,
    [splitDataUrl(pngUrl('SHOT')), splitDataUrl(pngUrl('IMG_a.png')), splitDataUrl(pngUrl('IMG_b.png'))]);

  // Turn 2: one new image → prior turn is trimmed to its most recent single image.
  await controller.addAttachment(stubFile('c.png', 'image/png'));
  await controller.send('and this');
  const m2 = client.calls[1].messages;
  assert.deepStrictEqual(m2[0].images, [splitDataUrl(pngUrl('IMG_b.png'))]);   // last image only
  assert.strictEqual(m2[1].images, undefined);                                 // assistant: text-only
  assert.deepStrictEqual(m2[2].images,
    [splitDataUrl(pngUrl('SHOT')), splitDataUrl(pngUrl('IMG_c.png'))]);        // current turn

  // Turn 3: no attachments → the turn still carries its own snapshot, and of the prior
  // turns only the single most recent image survives.
  await controller.send('text only now');
  const m3 = client.calls[2].messages;
  assert.strictEqual(m3[0].images, undefined);                                 // a/b turn now text-only
  assert.deepStrictEqual(m3[2].images, [splitDataUrl(pngUrl('IMG_c.png'))]);
  assert.deepStrictEqual(m3[4].images, [splitDataUrl(pngUrl('SHOT'))]);
});

// ── §7 working-image auto-attach ────────────────────────────────────────────
// The turn is grounded in what the editor is showing. Without this a question about
// the picture ("outline the rabbit's head") is answered from imagination — the model
// never saw a pixel. The desktop has always done it; this is the browser side.
test('every turn attaches the working image, even with nothing queued', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);
  await controller.send('what is in this picture?');
  assert.deepStrictEqual(client.calls[0].messages[0].images, [splitDataUrl(pngUrl('SHOT'))]);
});

test('an empty editor sends no snapshot — the turn is still a valid conversation', async () => {
  const client = makeClient(chatOnlyReply);
  const { stencil } = makeStencil();
  stencil.imageSize = null;                        // nothing loaded
  const controller = createChatController({
    stencil, getClient: () => client,
    workingSnapshot: async () => { throw new Error('nothing to export'); },
  });
  const entry = await controller.send('hello');
  assert.strictEqual(entry.reply, chatOnlyReply);
  assert.strictEqual(client.calls[0].messages[0].images, undefined);
});

test('a snapshot that fails to render is skipped, not fatal', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client, {
    workingSnapshot: async () => { throw new Error('canvas is tainted'); },
  });
  await controller.send('hi');
  assert.strictEqual(client.calls[0].messages[0].images, undefined);
});

// ── §7 edge map: a contour render of the snapshot rides second, wire-only ──
const edgeUrl = pngUrl('EDGE');

test('the edge map rides directly after the snapshot, and the suffix sentence rides with it', async () => {
  const client = makeClient(chatOnlyReply);
  const seen = [];
  const { controller } = makeController(client, {
    edgeMap: async (url) => { seen.push(url); return edgeUrl; },
  });
  await controller.addAttachment(stubFile('a.png', 'image/png'));
  await controller.send('outline the cat');
  // The edge map is rendered FROM the snapshot (same pixel coordinates)…
  assert.deepStrictEqual(seen, [pngUrl('SHOT')]);
  // …and attached second: snapshot, edge map, then the user's own attachment.
  assert.deepStrictEqual(client.calls[0].messages.at(-1).images, [
    splitDataUrl(pngUrl('SHOT')),
    splitDataUrl(edgeUrl),
    splitDataUrl(pngUrl('IMG_a.png')),
  ]);
  assert.ok(client.calls[0].system.endsWith(EDGE_MAP_SENTENCE));
});

test('a failed edge-map render attaches nothing — and then the sentence stays out', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client, {
    edgeMap: async () => { throw new Error('no canvas here'); },
  });
  await controller.send('hi');
  assert.deepStrictEqual(client.calls[0].messages[0].images, [splitDataUrl(pngUrl('SHOT'))]);
  assert.ok(!client.calls[0].system.includes(EDGE_MAP_SENTENCE));
});

test('an empty editor renders no edge map at all', async () => {
  const client = makeClient(chatOnlyReply);
  let edgeCalls = 0;
  const { controller } = makeController(client, {
    workingSnapshot: async () => { throw new Error('nothing to export'); },
    edgeMap: async () => { edgeCalls++; return edgeUrl; },
  });
  await controller.send('hello');
  assert.strictEqual(edgeCalls, 0);
  assert.ok(!client.calls[0].system.includes(EDGE_MAP_SENTENCE));
});

test('the edge map is current-turn only: never in history, never replayed', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client, { edgeMap: async () => edgeUrl });
  await controller.send('turn one');
  // History holds the snapshot only — the edge map went out on the wire alone.
  assert.deepStrictEqual(controller.history[0].images, [splitDataUrl(pngUrl('SHOT'))]);
  await controller.send('turn two');
  const m2 = client.calls[1].messages;
  // The replayed "single most recent prior image" is the snapshot, never the edge map…
  assert.deepStrictEqual(m2[0].images, [splitDataUrl(pngUrl('SHOT'))]);
  // …and only the current turn carries a fresh edge map, again second.
  assert.deepStrictEqual(m2.at(-1).images, [splitDataUrl(pngUrl('SHOT')), splitDataUrl(edgeUrl)]);
});

test('the text-only-model latch strips the edge map with the rest', async () => {
  const calls = [];
  let rejected = false;
  const client = {
    calls,
    chat: async ({ system, messages }) => {
      calls.push({ system, messages });
      if (!rejected && messages.some((m) => m.images)) {
        rejected = true;
        throw new Error('model does not support vision');
      }
      return chatOnlyReply;
    },
  };
  const { controller } = makeController(client, { edgeMap: async () => edgeUrl });
  await controller.send('make it sepia');
  assert.strictEqual(calls.length, 2);
  assert.ok(calls[0].system.endsWith(EDGE_MAP_SENTENCE));    // the first try carried it
  assert.ok(calls[1].messages.every((m) => !m.images));      // retry: every image stripped
  assert.ok(!calls[1].system.includes(EDGE_MAP_SENTENCE));   // the sentence went with them
  // Latched: the next turn attaches neither snapshot nor edge map.
  await controller.send('again');
  assert.ok(calls[2].messages.every((m) => !m.images));
  assert.ok(!calls[2].system.includes(EDGE_MAP_SENTENCE));
});

test('a text-only model: retry once without images, warn, and stop attaching', async () => {
  const calls = [];
  let failedOnce = false;
  const client = {
    calls,
    chat: async ({ messages }) => {
      calls.push({ messages });
      // The first call carries an image and is rejected the way a text-only
      // endpoint rejects one; everything after it must succeed.
      if (!failedOnce && messages.some((m) => m.images)) {
        failedOnce = true;
        throw Object.assign(new Error('model does not support multimodal input'), { kind: 'http' });
      }
      return chatOnlyReply;
    },
  };
  const { controller } = makeController(client);

  const entry = await controller.send('make it sepia');
  assert.strictEqual(entry.reply, chatOnlyReply);
  assert.match(entry.warnings.join(' '), /text-only/);
  assert.strictEqual(calls.length, 2);                      // one rejection + one retry
  assert.strictEqual(calls[1].messages[0].images, undefined);   // retried without the image
  assert.strictEqual(controller.history[0].images, undefined);  // and history was stripped

  // Latched: the next turn does not pay for the rejection again.
  await controller.send('and rotate it');
  assert.strictEqual(calls.length, 3);
  assert.ok(calls[2].messages.every((m) => !m.images));

  // A fresh conversation re-tries — the user may have switched to a vision model.
  controller.clearConversation();
  await controller.send('now what do you see?');
  assert.deepStrictEqual(calls[3].messages[0].images, [splitDataUrl(pngUrl('SHOT'))]);
});

test('a non-image error is not retried — it propagates untouched', async () => {
  let n = 0;
  const client = {
    calls: [],
    chat: async () => { n++; throw Object.assign(new Error('Response truncated'), { kind: 'truncated' }); },
  };
  const { controller } = makeController(client);
  await assert.rejects(() => controller.send('hi'), /truncated/);
  assert.strictEqual(n, 1);
});

test('replayMessages bounds history to the most recent 32 messages', () => {
  const history = Array.from({ length: 40 }, (_, i) => ({ role: i % 2 ? 'assistant' : 'user', text: `m${i}` }));
  const out = replayMessages(history);
  assert.strictEqual(out.length, HISTORY_LIMIT);
  assert.strictEqual(out[0].text, 'm8');
  assert.strictEqual(out[31].text, 'm39');
});

test('controller history itself stays bounded to 32', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);
  for (let i = 0; i < 20; i++) await controller.send(`msg ${i}`);   // 40 entries total
  assert.strictEqual(controller.history.length, HISTORY_LIMIT);
  assert.strictEqual(controller.history[0].text, 'msg 4');          // oldest trimmed
});

test('attachments: working images load into the editor; analyze-only ones do not', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller, calls } = makeController(client);
  await controller.addAttachment(stubFile('use-me.png', 'image/png'));
  await controller.addAttachment(stubFile('look-only.png', 'image/png'));
  controller.setAttachmentUse(0, 'working');
  await controller.send('go');
  assert.deepStrictEqual(calls, [['load', pngUrl('IMG_use-me.png'), { name: 'use-me.png' }]]);
  assert.strictEqual(controller.attachments.length, 0);   // pending list consumed
});

test('video attachments attach sampled frames; marking one working enables frame ops', async () => {
  const framePlan = JSON.stringify({ version: 1, reply: 'frame picked', actions: [{ op: 'frame', index: 30 }] });
  const client = makeClient([chatOnlyReply, framePlan]);
  const { controller, calls } = makeController(client);

  const at = await controller.addAttachment(stubFile('clip.mp4', 'video/mp4'));
  assert.strictEqual(at.kind, 'video');
  assert.strictEqual(at.frames.length, 4);
  controller.setAttachmentUse(0, 'working');
  await controller.send('here is a video');
  // The video itself is never sent — its sampled frames ride as images, behind the
  // working-image snapshot the turn always carries.
  assert.strictEqual(client.calls[0].messages[0].images.length, 5);
  assert.deepStrictEqual(client.calls[0].messages[0].images[0], splitDataUrl(pngUrl('SHOT')));
  assert.deepStrictEqual(client.calls[0].messages[0].images[1], splitDataUrl(pngUrl('FR0_clip.mp4')));
  assert.ok(client.calls[0].system.includes('video (clip.mp4)'));

  // A follow-up frame op resolves through frameAt + loadImage.
  await controller.send('grab frame 30');
  assert.deepStrictEqual(calls.at(-1), ['load', pngUrl('AT30_clip.mp4'), { name: 'frame30' }]);
});

test('frame ops without a video input reject (plan-level error, nothing executed)', async () => {
  const framePlan = JSON.stringify({ version: 1, reply: 'x', actions: [{ op: 'frame', index: 0 }] });
  const client = makeClient(framePlan);
  const { controller, calls } = makeController(client);
  await assert.rejects(() => controller.send('frame please'), /video/);
  assert.deepStrictEqual(calls, []);
});

test('client errors propagate without touching the facade (truncated/refusal handling)', async () => {
  const err = Object.assign(new Error('Response truncated'), { kind: 'truncated' });
  const client = { calls: [], chat: async () => { throw err; } };
  const { controller, calls } = makeController(client);
  await assert.rejects(() => controller.send('hi'), /truncated/);
  assert.deepStrictEqual(calls, []);
  // The user turn stays in history; no assistant turn was recorded.
  assert.deepStrictEqual(controller.history,
    [{ role: 'user', text: 'hi', images: [splitDataUrl(pngUrl('SHOT'))] }]);
});

test('rejected attachment types throw and queue nothing', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);
  await assert.rejects(() => controller.addAttachment(stubFile('doc.pdf', 'application/pdf')), /Not an image or video/);
  assert.strictEqual(controller.attachments.length, 0);
});

// ── Clipboard / drop extraction shared with the global paste wiring ──
const { mediaFilesFromData } = await import('../js/core/dragImageUrl.js');

test('mediaFilesFromData: pulls image/video FILES from clipboardData items, synchronously', () => {
  const img = stubFile('shot.png', 'image/png');
  const vid = stubFile('clip.mp4', 'video/mp4');
  const dt = {
    items: [
      { kind: 'string', type: 'text/plain', getAsFile: () => null },
      { kind: 'file', type: 'image/png', getAsFile: () => img },
      { kind: 'file', type: 'application/pdf', getAsFile: () => stubFile('doc.pdf', 'application/pdf') },
      { kind: 'file', type: 'video/mp4', getAsFile: () => vid },
      { kind: 'file', type: 'image/gif', getAsFile: () => null },   // unreadable → skipped
    ],
    files: [],
  };
  assert.deepStrictEqual(mediaFilesFromData(dt), [img, vid]);
});

test('mediaFilesFromData: falls back to .files (drop without items); tolerates junk', () => {
  const img = stubFile('drop.jpg', 'image/jpeg');
  assert.deepStrictEqual(
    mediaFilesFromData({ files: [img, stubFile('notes.txt', 'text/plain')] }), [img]);
  assert.deepStrictEqual(mediaFilesFromData(null), []);
  assert.deepStrictEqual(mediaFilesFromData({}), []);
  assert.deepStrictEqual(mediaFilesFromData({ items: [], files: [] }), []);
});

test('pasted/dropped media flows into attachments (image toggle + video frame path)', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);
  const dt = {
    items: [
      { kind: 'file', type: 'image/png', getAsFile: () => stubFile('paste.png', 'image/png') },
      { kind: 'file', type: 'video/webm', getAsFile: () => stubFile('paste.webm', 'video/webm') },
    ],
  };
  for (const f of mediaFilesFromData(dt)) await controller.addAttachment(f);
  assert.strictEqual(controller.attachments.length, 2);
  assert.deepStrictEqual(controller.attachments.map((a) => a.kind), ['image', 'video']);
  assert.strictEqual(controller.attachments[0].use, 'analyze');            // toggle available
  assert.strictEqual(controller.attachments[1].frames.length, 4);          // frame extraction ran
  controller.setAttachmentUse(0, 'working');
  assert.strictEqual(controller.attachments[0].use, 'working');
});

test('addImageDataUrl queues an analyze attachment; non-data URLs are rejected', async () => {
  const client = makeClient(chatOnlyReply);
  const { controller } = makeController(client);
  const at = controller.addImageDataUrl(pngUrl('SCRIPTED'), 'from-console.png');
  assert.deepStrictEqual(at, { name: 'from-console.png', kind: 'image', use: 'analyze', dataUrl: pngUrl('SCRIPTED') });
  assert.throws(() => controller.addImageDataUrl('http://x/y.png'), /data: URLs/);
  await controller.send('look');
  assert.deepStrictEqual(client.calls[0].messages[0].images,
    [splitDataUrl(pngUrl('SHOT')), splitDataUrl(pngUrl('SCRIPTED'))]);
});

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

// ── An attached picture the prompt asks you to ACT ON becomes the working image ──
// The model could otherwise only answer "the canvas is empty — drop it in", which is a
// refusal the user cannot act on when the picture is right there in the message.
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

// ── §2.1 multi-image plans: the turn's attachments become working images in turn ──

const multiImagePlan = JSON.stringify({
  version: 1, reply: 'Done.', actions: [
    { op: 'image', index: 1 }, { op: 'filter', mode: 'bw' }, { op: 'save' },
    { op: 'image', index: 2 }, { op: 'filter', mode: 'sepia' }, { op: 'save', name: 'second' },
  ], variants: [],
});

test('a multi-image plan works each attachment in turn and saves one project per image', async () => {
  const client = makeClient(multiImagePlan);
  const saved = [];
  const { controller, calls } = makeController(client, { saveProject: async (n) => { saved.push(n); } });
  await controller.addAttachment(stubFile('cat.jpg', 'image/jpeg'));
  await controller.addAttachment(stubFile('dog.png', 'image/png'));
  await controller.send('crop both and save them');
  // Each `image` op loaded THAT attachment as the working image, in plan order…
  const loads = calls.filter(([op]) => op === 'load').map(([, url]) => url);
  assert.deepStrictEqual(loads, [pngUrl('IMG_cat.jpg'), pngUrl('IMG_dog.png')]);
  // …and each `save` named its project after the image it was working on.
  assert.deepStrictEqual(saved, ['cat', 'second']);
});

test('an image index beyond what the message attached warns instead of failing the turn', async () => {
  const client = makeClient(JSON.stringify({
    version: 1, reply: 'Done.', actions: [{ op: 'image', index: 4 }, { op: 'filter', mode: 'bw' }],
    variants: [],
  }));
  const { controller, calls } = makeController(client);
  await controller.addAttachment(stubFile('cat.jpg', 'image/jpeg'));
  const out = await controller.send('do the fourth one');
  assert.ok(out.warnings.some((w) => w.includes('attached image 4')));
  assert.ok(calls.some(([op, arg]) => op === 'apply' && arg.filter === 'bw'));
});

// ── §7 auto-continuation: a plan that loads (and drew no layout) continues once ──
const CONTINUATION_NOTE = '[The working image is now the picture those actions loaded — continue with it.]';
const catUrl = 'http://pics.example/cat.png';
const mixedLoadPlan = JSON.stringify({
  version: 1, reply: 'Loaded and edited.', actions: [
    { op: 'openUrl', url: catUrl },
    { op: 'crop', spec: { x1: '10%' } },
    { op: 'filter', mode: 'bw' },
  ],
});

test('a load + crop + filter plan (no layout) runs its edits, then continues once', async () => {
  const client = makeClient([mixedLoadPlan, chatOnlyReply]);
  const { controller, calls } = makeController(client);
  const out = await controller.send(`load ${catUrl}, crop 10%, b&w and outline the face`);
  // Every action ran BEFORE the continuation…
  assert.deepStrictEqual(calls.filter(([op]) => op === 'load'), [['load', catUrl, undefined]]);
  assert.deepStrictEqual(calls.filter(([op]) => op === 'crop'), [['crop', { x1: '10%' }]]);
  assert.ok(calls.some(([op, arg]) => op === 'apply' && arg.filter === 'bw'));
  // …then exactly one more round went out, carrying the note + a fresh snapshot.
  assert.strictEqual(client.calls.length, 2);
  const note = client.calls[1].messages.at(-1);
  assert.strictEqual(note.role, 'user');
  assert.strictEqual(note.text, CONTINUATION_NOTE);
  assert.deepStrictEqual(note.images, [splitDataUrl(pngUrl('SHOT'))]);
  // The merged result keeps round 1's execution notes and round 2's reply.
  assert.ok(out.warnings.some((w) => w.includes(`Loaded ${catUrl}`)));
  assert.strictEqual(out.reply, chatOnlyReply);
});

test('a plan that loads but also drew a layout committed to its coordinates — no continuation', async () => {
  const drawn = { points: [{ x: 10, y: 10 }, { x: 90, y: 12 }, { x: 88, y: 100 }], color: '#ff0000' };
  const client = makeClient([JSON.stringify({
    version: 1, reply: 'Loaded and outlined.', actions: [
      { op: 'openUrl', url: catUrl },
      { op: 'layout', lines: [drawn] },
    ],
  })]);
  const { controller, stencil } = makeController(client);
  stencil.setLines = () => {};
  await controller.send(`load ${catUrl} and outline the cat`);
  // §3.0: the turn ends with the plan. One round — no continuation (it traced), and no
  // post-plan pass either.
  assert.strictEqual(client.calls.length, 1);
  const flat = client.calls.flatMap((c) => c.messages.map((m) => m.text));
  assert.ok(!flat.includes(CONTINUATION_NOTE), 'the continuation note never went out');
});

test('the continuation is bounded to one round — a second load plan stops the chain', async () => {
  const client = makeClient([mixedLoadPlan, JSON.stringify({
    version: 1, reply: 'Loaded again.', actions: [{ op: 'blank', color: '#ffffff' }],
  })]);
  const { controller } = makeController(client);
  const out = await controller.send(`load ${catUrl} and go`);
  assert.strictEqual(client.calls.length, 2, 'round 2 executed normally, no round 3');
  assert.strictEqual(out.reply, 'Loaded again.');
});

// Incognito is adopted in THIS editor, so the picture is here and unseen — §7 continues
// exactly as a plain openUrl does. Before the fix it handed off to another tab, and the
// model never got to look at what it had just fetched.
test('openUrl with incognito loads HERE, so §7 still continues once', async () => {
  const client = makeClient([JSON.stringify({
    version: 1, reply: 'Opened it incognito.', actions: [{ op: 'openUrl', url: catUrl, incognito: true }],
  }), JSON.stringify({ version: 1, reply: 'Now I can see it.', actions: [] })]);
  const opened = [];
  const { controller } = makeController(client, { openIncognito: async (u) => { opened.push(u); } });
  const out = await controller.send(`open ${catUrl} in incognito`);
  assert.deepStrictEqual(opened, [catUrl]);
  assert.strictEqual(client.calls.length, 2, 'the loaded picture came back for a second round');
  assert.strictEqual(out.reply, 'Now I can see it.');
  assert.ok(out.warnings.some((w) => w.includes('fresh incognito editor')));
});

// The whole user report in one turn: "<url> upload in incognito mode, make b&w, crop to
// portrait, highlight head/face/body, turn on horizontal comparison". Every action runs in
// order on the fetched picture, the §7 continuation traces it, and the conversation the
// user is looking at survives all of it.
test('an incognito openUrl plan runs every later action and keeps the conversation', async () => {
  const client = makeClient([
    JSON.stringify({
      version: 1, reply: 'Loading it incognito, then b&w + portrait crop.',
      actions: [
        { op: 'openUrl', url: catUrl, incognito: true },
        { op: 'filter', mode: 'bw' },
        { op: 'crop', spec: { aspect: '3:4' } },
        { op: 'compare', mode: 'horizontal' },
      ],
    }),
    JSON.stringify({
      version: 1, reply: 'Head, face and body outlined.',
      actions: [{ op: 'layout', lines: [{ points: [{ x: 10, y: 10 }, { x: 90, y: 120 }] }] }],
    }),
  ]);
  let adopted = null;
  const { controller, stencil, calls } = makeController(client, {
    openIncognito: async (u) => { adopted = u; stencil.imageSize = { width: 300, height: 400 }; },
  });
  let drawn = null;
  stencil.setLines = (ls) => { drawn = ls; };

  const out = await controller.send(
    `${catUrl} upload in incognito mode, bame b&w, crop to portrait, highkight bead , face and budy parts, turn on horz comparison`);

  assert.strictEqual(adopted, catUrl, 'the URL was adopted into THIS editor');
  // Plan order, on the fetched picture — nothing dropped, nothing raced.
  assert.deepStrictEqual(
    calls.filter(([op]) => ['apply', 'crop', 'compareMode'].includes(op)),
    [['apply', { filter: 'bw' }], ['crop', { aspect: '3:4' }], ['compareMode', 'horizontal']]);
  // §7: the continuation SAW the picture, so the outline landed.
  assert.strictEqual(client.calls.length, 2);
  assert.strictEqual(drawn?.length, 1);
  assert.strictEqual(out.reply, 'Head, face and body outlined.');
  // The conversation is intact: the user's message, both model turns, and the
  // continuation note are all still in the replay history.
  const roles = controller.history.map((m) => m.role);
  assert.deepStrictEqual(roles, ['user', 'assistant', 'user', 'assistant']);
  assert.match(controller.history[0].text, /upload in incognito mode/);
});

// §1 leniency, end-to-end: the user's report was a `clear` inside a variant killing the
// whole turn with "Could not read the assistant's plan". Now the turn lands normally.
test('a variant holding a settings op is dropped — the turn still runs and replies', async () => {
  const client = makeClient(JSON.stringify({
    version: 1, reply: 'Two takes.',
    actions: [{ op: 'filter', mode: 'bw' }],
    variants: [
      { label: 'start over', actions: [{ op: 'clear' }] },
      { label: 'turned', actions: [{ op: 'rotate', dir: 'left' }] },
    ],
  }));
  const { controller, calls } = makeController(client);
  const out = await controller.send('two takes please');
  assert.strictEqual(out.reply, 'Two takes.');
  assert.ok(!out.chatOnly);
  assert.deepStrictEqual(out.results.map((r) => r.label), ['turned']);
  assert.ok(calls.some(([op, a]) => op === 'apply' && a.filter === 'bw'), 'the top-level action ran');
  assert.ok(out.warnings.some((w) => /Dropped variant 1 \("start over"\)/.test(w)), JSON.stringify(out.warnings));
});

// ── The §10 expansion, wired end-to-end through a chat turn ──────────────────

test('compare/zoom/undo settings plan dispatches through the facade view controls', async () => {
  const viewPlan = JSON.stringify({
    version: 1, reply: 'view set', actions: [
      { op: 'compare', mode: 'vertical', split: 0.4 },
      { op: 'zoom', percent: 150 },
      { op: 'undo', steps: 2 },
      { op: 'zoom', fit: true },
    ],
  });
  const { controller, calls } = makeController(makeClient(viewPlan));
  const entry = await controller.send('compare side by side at 40%, zoom in, undo twice, then fit');
  assert.strictEqual(entry.reply, 'view set');
  assert.deepStrictEqual(calls, [
    ['compareMode', 'vertical'], ['compareSplit', 0.4],
    ['zoomLevel', 150],
    ['undo'], ['undo'],
    ['zoomFit'],
  ]);
});

test('renameProject routes through the injected capability; the store\'s refusal is a note', async () => {
  const renamePlan = JSON.stringify({
    version: 1, reply: 'renamed', actions: [{ op: 'renameProject', name: 'Taken' }],
  });
  const renamed = [];
  const { controller } = makeController(makeClient(renamePlan), {
    renameActiveProject: async (n) => { renamed.push(n); return `a project named "${n}" already exists`; },
  });
  const out = await controller.send('rename this project to Taken');
  assert.deepStrictEqual(renamed, ['Taken']);
  assert.ok(out.warnings.some((w) => w.includes('renameProject: a project named "Taken" already exists')));
});

test('blankColor on a non-blank project skips with the capability\'s note', async () => {
  const recolorPlan = JSON.stringify({
    version: 1, reply: 'recoloured', actions: [{ op: 'blankColor', color: '#dbeafe' }],
  });
  const { controller } = makeController(makeClient(recolorPlan), {
    setBlankColor: async () => 'only a blank project has a recolourable background',
  });
  const out = await controller.send('make the background light blue');
  assert.ok(out.warnings.some((w) => w.includes('blankColor: only a blank project has a recolourable background')));
});

test('incognito on a loaded editor: the facade throw becomes a note, not a failed turn', async () => {
  const incognitoPlan = JSON.stringify({
    version: 1, reply: 'ok', actions: [{ op: 'incognito', on: true }],
  });
  const { controller } = makeController(makeClient(incognitoPlan));
  const out = await controller.send('go incognito');
  assert.strictEqual(out.reply, 'ok');
  assert.ok(out.warnings.some((w) => w.includes('incognito: Incognito can only be enabled on a blank editor')));
});

test('openProject routes through the injected capability, notes included', async () => {
  const openPlan = JSON.stringify({
    version: 1, reply: 'opened', actions: [{ op: 'openProject', name: 'Cat' }],
  });
  const opened = [];
  const { controller } = makeController(makeClient(openPlan), {
    openProjectNamed: async (n) => { opened.push(n); return 'open canceled'; },
  });
  const out = await controller.send('open my Cat project');
  assert.deepStrictEqual(opened, ['Cat']);
  assert.ok(out.warnings.some((w) => w.includes('openProject: open canceled')));
});

test('copy what:"layout" awaits the injected outcome promise; a blocked write is a warning', async () => {
  const copyPlan = JSON.stringify({
    version: 1, reply: 'copied', actions: [{ op: 'copy', what: 'layout' }],
  });
  let wrote = 0;
  const okRun = makeController(makeClient(copyPlan), { copyLayoutRendered: async () => { wrote++; } });
  okRun.stencil.lines = [{ points: [{ x: 1, y: 1 }] }];
  const good = await okRun.controller.send('copy the layout json');
  assert.strictEqual(wrote, 1);
  assert.deepStrictEqual(good.warnings, []);

  const denied = makeController(makeClient(copyPlan), {
    copyLayoutRendered: async () => { throw new Error('Write permission denied'); },
  });
  denied.stencil.lines = [{ points: [{ x: 1, y: 1 }] }];
  const out = await denied.controller.send('copy the layout json');
  assert.ok(out.warnings.some((w) => w.includes('Copy to clipboard failed — Write permission denied')));
});

// ── An auxiliary round can never hold the turn open ─────────────────────────
// Reported: every edit landed on the canvas, the chat kept spinning, and only Stop
// ended it — a §3.2 correction request that never came back was holding the send.
// ── §3.0: the turn ends when its plan has executed ──────────────────────────
// The withdrawn §3.1 refinement / §3.2 correction ran extra model rounds AFTER the
// answer — one per traced line plus up to three serial correction rounds — so a
// 17-line trace spent minutes with the canvas already finished. No client runs them.
test('a layout plan issues EXACTLY ONE model round — nothing follows the reply', async () => {
  const lines = Array.from({ length: 17 }, (_, i) => ({
    points: [{ x: 10 + i, y: 10 }, { x: 90 + i, y: 12 }, { x: 88 + i, y: 100 }],
    color: '#ff0000',
  }));
  const client = makeClient([JSON.stringify({
    version: 1, reply: 'Outlined all seventeen.', actions: [{ op: 'layout', lines }],
  })]);
  const { controller, stencil } = makeController(client);
  let drawn = [];
  stencil.setLines = (ls) => { drawn = ls; };
  Object.defineProperty(stencil, 'lines', { get: () => drawn });
  const entry = await controller.send('outline everything');
  // ONE round. Seventeen lines used to mean seventeen more.
  assert.strictEqual(client.calls.length, 1, 'the plan turn, and only the plan turn');
  assert.strictEqual(entry.reply, 'Outlined all seventeen.');
  assert.strictEqual(drawn.length, 17, 'the model\'s traced lines ARE the result');
  // No note about sharpening or self-checks can ever ride the reply.
  assert.deepStrictEqual(entry.warnings, []);
  assert.ok(!/sharpen|self-check|correction/i.test(JSON.stringify(entry)));
  // …and nothing keeps running: a later tick issues no further calls.
  await new Promise((r) => setTimeout(r, 20));
  assert.strictEqual(client.calls.length, 1, 'still one — nothing runs in the background');
});

test('the withdrawn machinery is gone from the controller\'s surface', async () => {
  const mod = await import('../js/llm/chatController.js');
  for (const gone of ['auxDeadline', 'AUX_ROUND_TIMEOUT_MS', 'findSuspectLines',
    'LAYOUT_CORRECTION_PROMPT', 'REFINE_MAX_PARALLEL', 'REFINE_MAX_RENDER_ZOOM']) {
    assert.strictEqual(mod[gone], undefined, `${gone} must not exist`);
  }
  const { controller } = makeController(makeClient(['hi']));
  for (const gone of ['cancelAux', 'auxRunning']) {
    assert.strictEqual(controller[gone], undefined, `controller.${gone} must not exist`);
  }
  // §7's own attachments STAY — the working-image snapshot and its edge map are what
  // the main turn sends, and they are not part of the withdrawn passes.
  assert.strictEqual(typeof mod.EDGE_MAP_SENTENCE, 'string');
  assert.match(mod.EDGE_MAP_SENTENCE, /edge-map render of the working image/);
  assert.strictEqual(typeof mod.contourDataUrl, 'function');
  const src = readFileSync(new URL('../js/llm/chatController.js', import.meta.url), 'utf8');
  assert.ok(!/refineOutlines|correctDrawnLayout|refineDrawnOutlines|startAuxChain/.test(src));
  assert.ok(!/could not be sharpened/.test(src), 'no sharpening note can be produced');
});
