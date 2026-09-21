// §7 edge map (js/llm/controller.js): a contour render rides second, wire-only, and the
// text-only-model latch strips it along with the rest of the images.
import { test } from 'node:test';
import assert from 'node:assert';
import { splitDataUrl, EDGE_MAP_SENTENCE } from '../js/llm/chat/controller.js';
import { makeClient, chatOnlyReply, pngUrl, stubFile, makeController } from './helpers/chatControllerRig.js';

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
