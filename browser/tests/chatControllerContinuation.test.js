// §7 auto-continuation (js/llm/controller.js): a plan that loads a picture continues
// once, a layout plan does not, and the chain is bounded to one extra round.
import { test } from 'node:test';
import assert from 'node:assert';
import { splitDataUrl } from '../js/llm/chat/controller.js';
import { makeClient, chatOnlyReply, pngUrl, makeController } from './helpers/chatControllerRig.js';

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

// Incognito is adopted in THIS editor, so the picture is here and unseen and §7 continues exactly as
// a plain openUrl does — handed off to another tab, the model never sees what it fetched.
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

// One turn end to end: every action runs in order on the fetched picture, the §7 continuation traces
// it, and the conversation the user is looking at survives all of it.
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
