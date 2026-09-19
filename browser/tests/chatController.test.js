// createChatController (js/llm/chatController.js): the wire shape of a turn — variant
// exports, the history record, and the system prompt the browser profile assembles.
import { test } from 'node:test';
import assert from 'node:assert';
import PROMPT_ASSET from '../js/config/llm/systemPrompt.json' with { type: 'json' };
import { splitDataUrl } from '../js/llm/chatController.js';
import {
  makeClient, chatOnlyReply, variantPlan, pngUrl, makeController,
} from './helpers/chatControllerRig.js';

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
