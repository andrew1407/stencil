// The §8 panel-op widening in src/llm/controller.js — rescan, pin/unpin, accent and
// openUrl — each acting through an injected capability, never auto-continuing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { LISTING, makeController } from '../helpers/chatHarness.js';

test('rescan-only plan: refreshes the listing, notes it in history, auto-continues once', async () => {
  const FRESH = [...LISTING, { kind: 'img', src: 'https://a.example/late.png', w: 40, h: 40 }];
  let working = null;
  const { controller, calls } = makeController([
    JSON.stringify({ version: 1, reply: 'Rescanning.', actions: [{ op: 'rescan' }], variants: [] }),
    'A fourth image appeared.',
  ], {
    getListing: () => working || LISTING,
    rescan: async () => { working = FRESH; return { ok: true, count: FRESH.length }; },
  });
  const result = await controller.send('the page changed — rescan it');
  assert.equal(calls.length, 2);   // exactly one continuation
  assert.deepEqual(result.cards, [{ kind: 'rescan', count: 4, ok: true }]);
  // The continuation round saw the REFRESHED listing in its suffix…
  assert.match(calls[1].system, /late\.png/);
  // …and the history explains the refresh as a user-side note.
  const note = controller.history.find((m) => /Re-scanned the current page/.test(m.text || ''));
  assert.ok(note && note.role === 'user');
  assert.match(note.text, /4 images/);
  assert.equal(result.continuation.reply, 'A fourth image appeared.');
});

test('a second rescan-only plan does NOT trigger another continuation (bounded to one)', async () => {
  const RESCAN_PLAN = JSON.stringify({ version: 1, reply: 'Rescanning.', actions: [{ op: 'rescan' }], variants: [] });
  const { controller, calls } = makeController([RESCAN_PLAN, RESCAN_PLAN], {
    rescan: async () => ({ ok: true, count: LISTING.length }),
  });
  const result = await controller.send('rescan twice');
  assert.equal(calls.length, 2);   // NOT three
  assert.equal(result.continuation.continuation, undefined);
  assert.ok(result.continuation.warnings.some((w) => /send another message/.test(w)));
});

test('a failed rescan keeps the turn alive: warning + failure card, no continuation', async () => {
  const { controller, calls } = makeController([
    JSON.stringify({ version: 1, reply: 'Rescanning.', actions: [{ op: 'rescan' }], variants: [] }),
  ], {
    rescan: async () => ({ ok: false, error: 'this page can’t be scanned' }),
  });
  const result = await controller.send('rescan');
  assert.equal(calls.length, 1);
  assert.ok(result.warnings.some((w) => /Could not re-scan the page/.test(w)));
  assert.deepEqual(result.cards, [{ kind: 'rescan', ok: false }]);
});

test('unpin executes per index through the injected capability and does NOT continue', async () => {
  const unpinned = [];
  const { controller, calls } = makeController([
    JSON.stringify({ version: 1, reply: 'Unpinned.', actions: [{ op: 'unpin', images: [1, 2] }], variants: [] }),
  ], {
    unpinImage: async (i, entry) => { assert.ok(entry); unpinned.push(i); },
  });
  const result = await controller.send('unpin the last two');
  assert.equal(calls.length, 1);   // unpin ACTS — no auto-continuation
  assert.deepEqual(unpinned, [1, 2]);
  assert.deepEqual(result.cards, [{ kind: 'unpin', indices: [1, 2], unpinned: [1, 2] }]);
});

test('an unpin failure warns per index and keeps the successes', async () => {
  const { controller } = makeController([
    JSON.stringify({ version: 1, reply: 'Unpinned.', actions: [{ op: 'unpin', images: [0, 1] }], variants: [] }),
  ], {
    unpinImage: async (i) => { if (i === 1) throw new Error('no stable source URL'); },
  });
  const result = await controller.send('unpin them');
  assert.ok(result.warnings.some((w) => /Could not unpin image 1/.test(w)));
  assert.deepEqual(result.cards, [{ kind: 'unpin', indices: [0, 1], unpinned: [0] }]);
});

test('accent runs through setAccent and surfaces the applied preset (nearest match marked)', async () => {
  const seen = [];
  const { controller } = makeController([
    JSON.stringify({ version: 1, reply: 'Recolouring.', actions: [{ op: 'accent', color: '#111111' }], variants: [] }),
  ], {
    setAccent: async (a) => { seen.push(a); return { label: 'Grey', exact: false }; },
  });
  const result = await controller.send('make the panel dark grey');
  assert.deepEqual(seen, [{ op: 'accent', color: '#111111' }]);
  assert.deepEqual(result.cards, [{ kind: 'accent', asked: '#111111', applied: 'Grey', exact: false, ok: true }]);
});

test('accent without the capability (or failing) warns instead of aborting the turn', async () => {
  const plan = JSON.stringify({ version: 1, reply: 'Recolouring.', actions: [{ op: 'accent', preset: 'teal' }], variants: [] });
  const none = makeController([plan]);
  const r1 = await none.controller.send('teal please');
  assert.deepEqual(r1.cards, []);
  assert.ok(r1.warnings.some((w) => /accent is not supported here/.test(w)));

  const failing = makeController([plan], { setAccent: async () => { throw new Error('unknown accent preset "teal"'); } });
  const r2 = await failing.controller.send('teal please');
  assert.deepEqual(r2.cards, [{ kind: 'accent', asked: 'teal', ok: false }]);
  assert.ok(r2.warnings.some((w) => /unknown accent preset "teal"/.test(w)));
});

test('pin executes per index through the injected capability and does NOT continue', async () => {
  const pinned = [];
  const { controller, calls } = makeController([
    JSON.stringify({ version: 1, reply: 'Pinned.', actions: [{ op: 'pin', images: [0, 2] }], variants: [] }),
  ], {
    pinImage: async (i, entry) => { assert.ok(entry); pinned.push(i); },
  });
  const result = await controller.send('pin the first and third');
  assert.equal(calls.length, 1);   // pin ACTS — no auto-continuation
  assert.deepEqual(pinned, [0, 2]);
  assert.deepEqual(result.cards, [{ kind: 'pin', indices: [0, 2], pinned: [0, 2] }]);
});

test('a pin failure warns per index and keeps the successes', async () => {
  const { controller } = makeController([
    JSON.stringify({ version: 1, reply: 'Pinned.', actions: [{ op: 'pin', images: [0, 1] }], variants: [] }),
  ], {
    pinImage: async (i) => { if (i === 1) throw new Error('no stable source URL'); },
  });
  const result = await controller.send('pin them');
  assert.ok(result.warnings.some((w) => /Could not pin image 1/.test(w)));
  assert.deepEqual(result.cards, [{ kind: 'pin', indices: [0, 1], pinned: [0] }]);
});

test('openUrl runs only for a URL the user typed; others warn and never fetch', async () => {
  const URL_OK = 'https://a.example/cat.jpg';
  const opened = [];
  const mk = (responses) => makeController(responses, { openUrlImage: async (a) => opened.push(a.url) });

  // Echoed from the user's message → opened.
  const ok = mk([JSON.stringify({ version: 1, reply: 'Opening.', actions: [{ op: 'openUrl', url: URL_OK, incognito: true }], variants: [] })]);
  const r1 = await ok.controller.send(`open ${URL_OK} in incognito`);
  assert.deepEqual(opened, [URL_OK]);
  assert.deepEqual(r1.cards, [{ kind: 'openUrl', url: URL_OK, incognito: true, ok: true }]);

  // A URL the user never wrote → blocked with a warning, capability untouched.
  opened.length = 0;
  const blocked = mk([JSON.stringify({ version: 1, reply: 'Opening.', actions: [{ op: 'openUrl', url: 'https://evil.example/x.png' }], variants: [] })]);
  const r2 = await blocked.controller.send('open the cat image please');
  assert.deepEqual(opened, []);
  assert.ok(r2.warnings.some((w) => /not a URL you gave/.test(w)));
  assert.deepEqual(r2.cards, [{ kind: 'openUrl', url: 'https://evil.example/x.png', ok: false }]);
});
