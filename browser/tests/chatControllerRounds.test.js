// §3.0 one round per turn (js/llm/chatController.js): nothing follows the reply, and the
// withdrawn refinement/correction machinery is gone from the controller's surface.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { EDGE_MAP_SENTENCE } from '../js/llm/chatController.js';
import { makeClient, makeController } from './helpers/chatControllerRig.js';

// §3.0: the turn ends when its plan has executed, and no auxiliary round may hold the send open. No
// client runs the withdrawn §3.1 refinement or §3.2 correction rounds.
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
