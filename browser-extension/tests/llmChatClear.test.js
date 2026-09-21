// §13 forbidden ops and the §10 clearChat op in src/llm/controller.js: the
// executor-level refusal, and the clear deferred to the very end of the turn.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { rejectForbidden } from '../src/llm/chatController.js';
import { ATTACH_PLAN, makeController } from './helpers/chatHarness.js';

test('rejectForbidden refuses a forbidden op with a warning and never executes it', () => {
  const x = { warnings: [], cards: [] };
  assert.equal(rejectForbidden({ op: 'shareTabs' }, x), true);
  assert.equal(rejectForbidden({ op: 'download' }, x), true);
  assert.ok(x.warnings.every((w) => /never model-drivable/.test(w)));
  assert.equal(x.warnings.length, 2);
  assert.deepEqual(x.cards, []);
  // Registered ops pass straight through, untouched.
  const y = { warnings: [], cards: [] };
  assert.equal(rejectForbidden({ op: 'focus', image: 0 }, y), false);
  assert.equal(rejectForbidden(null, y), false);
  assert.deepEqual(y.warnings, []);
});

test('clearChat runs LAST — after the plan\'s other ops — once, behind the confirm', async () => {
  const order = [];
  const { controller } = makeController([JSON.stringify({
    version: 1, reply: 'Clearing.', variants: [],
    actions: [{ op: 'focus', image: 0 }, { op: 'clearChat' }, { op: 'pin', image: 1 }, { op: 'clearChat' }],
  })], {
    focusImage: async (i) => { order.push(`focus${i}`); return true; },
    pinImage: async (i) => { order.push(`pin${i}`); },
    clearChat: async () => { order.push('confirm'); return true; },
  });
  const result = await controller.send('clear the chat');
  assert.deepEqual(order, ['focus0', 'pin1', 'confirm']);   // deferred, and deduped to one confirm
  assert.deepEqual(result.clearChat, { confirmed: true });
  assert.equal(controller.history.length, 0);               // the replay history is wiped
});

test('a declined confirm is a canceled clear, never a failed plan — history intact', async () => {
  const { controller } = makeController([JSON.stringify({
    version: 1, reply: 'Clearing.', actions: [{ op: 'clearChat' }], variants: [],
  })], { clearChat: async () => false });
  const result = await controller.send('clear it');
  assert.deepEqual(result.clearChat, { confirmed: false });
  assert.deepEqual(result.warnings, []);
  assert.deepEqual(controller.history.map((m) => m.role), ['user', 'assistant']);
});

test('clearChat asked by the CONTINUATION round still waits for that round to finish', async () => {
  let confirms = 0;
  const { controller, calls } = makeController([
    ATTACH_PLAN,
    JSON.stringify({ version: 1, reply: 'Now clearing.', actions: [{ op: 'clearChat' }], variants: [] }),
  ], {
    clearChat: async () => { confirms++; assert.equal(calls.length, 2); return true; },
  });
  const result = await controller.send('look at them, then clear the chat');
  assert.equal(calls.length, 2);
  assert.equal(confirms, 1);
  assert.deepEqual(result.clearChat, { confirmed: true });   // the OUTERMOST result carries it
  assert.equal(result.continuation.clearChat, undefined);
  assert.equal(controller.history.length, 0);
});

test('clearChat ACTS: a gather + clearChat plan never auto-continues', async () => {
  const { controller, calls } = makeController([JSON.stringify({
    version: 1, reply: 'ok', actions: [{ op: 'attach', image: 0 }, { op: 'clearChat' }], variants: [],
  })], { clearChat: async () => true });
  await controller.send('look then clear');
  assert.equal(calls.length, 1);
});

test('no clearChat capability warns; a throwing confirm reads as declined', async () => {
  const CLEAR_PLAN = JSON.stringify({ version: 1, reply: 'ok', actions: [{ op: 'clearChat' }], variants: [] });
  const none = makeController([CLEAR_PLAN]);
  const r1 = await none.controller.send('clear');
  assert.ok(r1.warnings.some((w) => /not supported here/.test(w)));
  assert.equal(r1.clearChat, undefined);
  assert.equal(none.controller.history.length, 2);

  const throwing = makeController([CLEAR_PLAN], {
    clearChat: async () => { throw new Error('dialog torn down'); },
  });
  const r2 = await throwing.controller.send('clear');
  assert.deepEqual(r2.clearChat, { confirmed: false });
  assert.equal(throwing.controller.history.length, 2);
});

test('a model plan naming a forbidden op drops it (§1 unknown-op skip); the rest still runs', async () => {
  const { controller, log } = makeController([
    JSON.stringify({ version: 1, reply: 'Sure.', actions: [{ op: 'download' }, { op: 'focus', image: 1 }], variants: [] }),
  ]);
  const result = await controller.send('download image 1');
  assert.deepEqual(log.focused, [1]);
  assert.equal(result.cards.length, 1);
  assert.equal(result.cards[0].kind, 'focus');
  assert.ok(result.warnings.some((w) => /Skipped unknown operation "download"/.test(w)));
});
