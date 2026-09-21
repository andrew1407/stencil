// §2.1 multi-image plans (js/llm/controller.js): each attachment becomes the working
// image in turn, one project saved per image, and an out-of-range index warns.
import { test } from 'node:test';
import assert from 'node:assert';
import { makeClient, pngUrl, stubFile, makeController } from '../../helpers/chatControllerRig.js';

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
