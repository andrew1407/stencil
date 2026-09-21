// The turn loop of src/llm/controller.js: what the system prompt carries, how cards and
// warnings form, and the bounded attach auto-continuation — over the scripted-client harness.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { matchListingIndex, attachmentNote } from '../src/llm/chatController.js';
import { LLM_SYSTEM_PROMPT } from '../src/llm/op/plan.js';
import { ATTACH_PLAN, LISTING, makeController } from './helpers/chatHarness.js';

// ── Controller: execution + bounded auto-continuation ──

test('system = verbatim prompt + page + listing suffix (never prepended)', async () => {
  const { controller, calls } = makeController(['just chatting']);
  await controller.send('hi');
  assert.ok(calls[0].system.startsWith(LLM_SYSTEM_PROMPT));
  assert.match(calls[0].system, /Current page: https:\/\/a\.example\/page/);
  assert.match(calls[0].system, /Images scanned from the current page/);
  assert.match(calls[0].system, /0: img 10x10 png "one\.png"/);
});

test('attach-only plan → fetch, attach, and auto-continue exactly once', async () => {
  const { controller, calls, log } = makeController([ATTACH_PLAN, 'The cat is image 2.']);
  const result = await controller.send('which of these is the cat?');

  assert.equal(calls.length, 2);                       // one continuation, no more
  assert.deepEqual(log.attached, [0, 2]);
  assert.equal(result.reply, 'Let me look at them.');
  assert.deepEqual(result.cards, [{ kind: 'attach', indices: [0, 2], attached: [0, 2] }]);
  assert.ok(result.continuation);
  assert.equal(result.continuation.reply, 'The cat is image 2.');
  assert.equal(result.continuation.chatOnly, true);
  assert.equal(result.continuation.continuation, undefined);

  // The continuation call replays the attach as a USER message carrying the images.
  const secondMsgs = calls[1].messages;
  const last = secondMsgs[secondMsgs.length - 1];
  assert.equal(last.role, 'user');
  assert.match(last.text, /Attached images 0, 2/);
  assert.deepEqual(last.images, [{ mediaType: 'image/png', data: 'IMG0' }, { mediaType: 'image/png', data: 'IMG2' }]);

  // History: user, assistant(raw plan), user(attached), assistant(answer).
  assert.deepEqual(controller.history.map((m) => m.role), ['user', 'assistant', 'user', 'assistant']);
});

test('a second attach-only plan does NOT trigger another continuation (bounded to one)', async () => {
  const { controller, calls } = makeController([ATTACH_PLAN, JSON.stringify({
    version: 1, reply: 'More please.', actions: [{ op: 'attach', image: 1 }], variants: [],
  })]);
  const result = await controller.send('look at everything');
  assert.equal(calls.length, 2);                       // NOT three
  assert.equal(result.continuation.continuation, undefined);
  assert.ok(result.continuation.warnings.some((w) => /send another message/.test(w)));
  // The second batch of images still lands in history for the NEXT user turn.
  const last = controller.history[controller.history.length - 1];
  assert.equal(last.role, 'user');
  assert.deepEqual(last.images, [{ mediaType: 'image/png', data: 'IMG1' }]);
});

test('two attach actions in one plan yield per-action cards but one combined history turn', async () => {
  const { controller, log } = makeController([JSON.stringify({
    version: 1, reply: 'Looking at both.', variants: [],
    actions: [{ op: 'attach', images: [0, 1] }, { op: 'attach', image: 2 }],
  }), 'done']);
  const result = await controller.send('look');
  // Each card lists ONLY its own action's indices…
  assert.deepEqual(result.cards, [
    { kind: 'attach', indices: [0, 1], attached: [0, 1] },
    { kind: 'attach', indices: [2], attached: [2] },
  ]);
  assert.deepEqual(log.attached, [0, 1, 2]);
  // …while the replayed user message carries all of them.
  const attachMsg = controller.history.find((m) => m.role === 'user' && m.images);
  assert.match(attachMsg.text, /Attached images 0, 1, 2 from the listing/);
  assert.equal(attachMsg.images.length, 3);
});

test('a failed fetch inside attach stays out of that action\'s card', async () => {
  const { controller } = makeController([JSON.stringify({
    version: 1, reply: 'Looking.', variants: [],
    actions: [{ op: 'attach', images: [0, 1] }],
  }), 'done'], {
    attachImage: async (i) => {
      if (i === 0) throw new Error('blocked');
      return { mediaType: 'image/png', data: `IMG${i}` };
    },
  });
  const result = await controller.send('look');
  assert.deepEqual(result.cards, [{ kind: 'attach', indices: [0, 1], attached: [1] }]);
  assert.ok(result.warnings.some((w) => /attach image 0.*blocked/.test(w)));
});

test('a mixed plan (focus + attach) executes both but does not auto-continue', async () => {
  const { controller, calls, log } = makeController([JSON.stringify({
    version: 1, reply: 'Highlighting and looking.', actions: [{ op: 'focus', image: 1 }, { op: 'attach', image: 0 }], variants: [],
  })]);
  const result = await controller.send('show me');
  assert.equal(calls.length, 1);
  assert.equal(result.continuation, undefined);
  assert.deepEqual(log.focused, [1]);
  assert.deepEqual(log.attached, [0]);
  // Attached images still wait in history for the next turn.
  const last = controller.history[controller.history.length - 1];
  assert.equal(last.role, 'user');
  assert.deepEqual(last.images, [{ mediaType: 'image/png', data: 'IMG0' }]);
});

test('focus and open surface as cards; open receives the validated action + entry', async () => {
  const seen = [];
  const { controller, log } = makeController([JSON.stringify({
    version: 1, reply: 'Done.', variants: [],
    actions: [
      { op: 'focus', image: 2 },
      { op: 'open', image: 1, incognito: true, actions: [{ op: 'page', format: 'a4' }] },
    ],
  })], {
    openImage: async (a, entry) => { seen.push({ a, entry }); return ['translated-warning']; },
  });
  const result = await controller.send('open the second one on a4');
  assert.equal(log.focused[0], 2);
  assert.equal(seen[0].a.op, 'open');
  assert.equal(seen[0].a.incognito, true);
  assert.deepEqual(seen[0].a.actions, [{ op: 'page', format: 'a4' }]);
  assert.equal(seen[0].entry, LISTING[1]);
  assert.deepEqual(result.cards.map((c) => c.kind), ['focus', 'open']);
  assert.equal(result.cards[0].ok, true);
  assert.equal(result.cards[1].ok, true);
  assert.deepEqual(result.warnings, ['translated-warning']);
});

test('a failing open becomes a failed card + warning instead of aborting the turn', async () => {
  const { controller } = makeController([JSON.stringify({
    version: 1, reply: 'Opening.', actions: [{ op: 'open', image: 0 }], variants: [],
  })], {
    openImage: async () => { throw new Error('fetch blocked'); },
  });
  const result = await controller.send('open it');
  assert.equal(result.cards[0].kind, 'open');
  assert.equal(result.cards[0].ok, false);
  assert.ok(result.warnings.some((w) => /fetch blocked/.test(w)));
});

test('a plan carrying variants still runs its actions — warning, no thrown turn (§1)', async () => {
  const { controller, log } = makeController([JSON.stringify({
    version: 1, reply: 'Highlighting.', actions: [{ op: 'focus', image: 1 }],
    variants: [{ label: 'rotated', actions: [{ op: 'rotate', dir: 'left' }] }],
  })]);
  const result = await controller.send('show me a rotated one');
  assert.deepEqual(log.focused, [1]);
  assert.equal(result.reply, 'Highlighting.');
  assert.ok(result.warnings.some((w) => /Skipped 1 variant/.test(w)));
});

test('plan warnings (dropped top-level core ops) ride the result', async () => {
  const { controller } = makeController([JSON.stringify({
    version: 1, reply: 'Cropping!', actions: [{ op: 'crop', spec: { x1: '10%' } }], variants: [],
  })]);
  const result = await controller.send('crop it');
  assert.deepEqual(result.cards, []);
  assert.ok(result.warnings.some((w) => /core operation "crop"/.test(w)));
});

// ── Dropped attachments: listing-match routing + send(text, { attachments }) ──

test('matchListingIndex matches a dropped URL to its scan entry (fragment-insensitive)', () => {
  const items = [
    { kind: 'img', src: 'https://a.example/one.png' },
    { kind: 'video', src: 'data:image/jpeg;base64,FRAME', videoUrl: 'https://a.example/clip.mp4' },
    { kind: 'bg', src: 'https://a.example/tile.jpg' },
  ];
  assert.equal(matchListingIndex(items, 'https://a.example/one.png'), 0);
  assert.equal(matchListingIndex(items, 'https://a.example/one.png#frag'), 0);       // dropped URL carries a hash
  assert.equal(matchListingIndex(items, 'https://a.example/clip.mp4'), 1);           // videos match their media URL…
  assert.equal(matchListingIndex(items, 'data:image/jpeg;base64,FRAME'), -1);    // …never their opaque still
  assert.equal(matchListingIndex(items, 'https://a.example/tile.jpg'), 2);
  assert.equal(matchListingIndex(items, 'https://a.example/other.png'), -1);
  assert.equal(matchListingIndex(items, ''), -1);
  assert.equal(matchListingIndex(null, 'https://a.example/one.png'), -1);
});

test('attachmentNote describes listing-matched and plain attachments', () => {
  assert.equal(attachmentNote([]), '');
  assert.equal(attachmentNote(null), '');
  assert.equal(
    attachmentNote([{ index: 2, name: 'cat.png' }, { name: 'photo.jpg' }, {}]),
    '[The user attached: image 2 from the listing (cat.png); photo.jpg; an image]');
  assert.equal(attachmentNote([{ index: 0 }]), '[The user attached: image 0 from the listing]');
});

test('send(text, { attachments }) rides the images on the user turn with the note', async () => {
  const { controller, calls } = makeController(['Looks like a cat.']);
  const attachments = [
    { image: { mediaType: 'image/png', data: 'D1' }, index: 1, name: 'two.png' },
    { image: { mediaType: 'image/png', data: 'D2' }, name: 'local.jpg' },
  ];
  const result = await controller.send('what is this?', { attachments });
  assert.equal(result.reply, 'Looks like a cat.');

  const msgs = calls[0].messages;
  const last = msgs[msgs.length - 1];
  assert.equal(last.role, 'user');
  assert.deepEqual(last.images, [{ mediaType: 'image/png', data: 'D1' }, { mediaType: 'image/png', data: 'D2' }]);
  assert.match(last.text, /^what is this\?/);
  assert.match(last.text, /image 1 from the listing \(two\.png\)/);
  assert.match(last.text, /local\.jpg/);
  // History keeps the images so the §7 replay rule applies on later turns.
  assert.deepEqual(controller.history[0].images.length, 2);
});

test('send with attachments but no images array entries stays a text-only message', async () => {
  const { controller, calls } = makeController(['ok']);
  await controller.send('plain', { attachments: [] });
  assert.equal(calls[0].messages[0].images, undefined);
  assert.equal(calls[0].messages[0].text, 'plain');
});

test('chat-only replies pass through untouched', async () => {
  const { controller } = makeController(['Hello! Ask me about the images.']);
  const result = await controller.send('hi');
  assert.equal(result.chatOnly, true);
  assert.equal(result.reply, 'Hello! Ask me about the images.');
  assert.deepEqual(result.cards, []);
});
