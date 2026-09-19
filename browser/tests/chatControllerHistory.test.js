// History bounds and attachments (js/llm/chatController.js): the 32-message cap, working vs
// analyze-only images, sampled video frames and the errors that never reach the facade.
import { test } from 'node:test';
import assert from 'node:assert';
import { replayMessages, splitDataUrl, HISTORY_LIMIT } from '../js/llm/chatController.js';
import { makeClient, chatOnlyReply, pngUrl, stubFile, makeController } from './helpers/chatControllerRig.js';

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
