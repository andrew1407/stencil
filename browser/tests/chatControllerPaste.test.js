// Clipboard / drop extraction shared with the global paste wiring (js/core/dragImageUrl.js):
// media files off the event, and how they flow into the controller's attachments.
import { test } from 'node:test';
import assert from 'node:assert';
import { splitDataUrl } from '../js/llm/chatController.js';
import { makeClient, chatOnlyReply, pngUrl, stubFile, makeController } from './helpers/chatControllerRig.js';

// ── Clipboard / drop extraction shared with the global paste wiring ──
const { mediaFilesFromData } = await import('../js/core/pointer/dragImageUrl.js');

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
