// §7 working-image attachment (js/llm/controller.js): the replay rule for prior turns,
// the snapshot every turn carries, and an empty editor sending none.
import { test } from 'node:test';
import assert from 'node:assert';
import { createChatController, splitDataUrl } from '../../../js/llm/chat/controller.js';
import {
  makeClient, makeStencil, chatOnlyReply, pngUrl, stubFile, makeController,
} from '../../helpers/chatControllerRig.js';

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

// §7 working-image auto-attach: the turn is grounded in what the editor is showing, or a question
// about the picture is answered from imagination. The desktop has always done it.
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
