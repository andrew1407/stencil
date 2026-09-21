// The §8 tabs listing and the scanTab op in src/llm/controller.js: what the model is
// shown about other open tabs, and the bounded auto-continuation a tab swap earns.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildTabsListing, TABS_LIMIT, TAB_TITLE_CHARS } from '../../src/llm/chatController.js';
import { LISTING, makeController } from '../helpers/chatHarness.js';

const TABS = [
  { tabId: 11, title: 'stencil/bot/assets at main', url: 'https://github.com/andrew1407/stencil/tree/main/bot/assets' },
  { tabId: 12, title: 'News', url: 'https://news.example.com/' },
];

test('buildTabsListing: index, truncated title, url; capped with an overflow note', () => {
  const lines = buildTabsListing(TABS).split('\n');
  assert.equal(lines[0], '0: "stencil/bot/assets at main" — https://github.com/andrew1407/stencil/tree/main/bot/assets');
  assert.equal(lines[1], '1: "News" — https://news.example.com/');
  const many = Array.from({ length: TABS_LIMIT + 3 }, (_, i) => ({ title: `t${i}`, url: `https://x/${i}` }));
  const capped = buildTabsListing(many).split('\n');
  assert.equal(capped.length, TABS_LIMIT + 1);
  assert.match(capped[TABS_LIMIT], /\+3 more not listed/);
  const [long] = buildTabsListing([{ title: 'z'.repeat(120), url: 'https://x/' }]).split('\n');
  assert.ok(long.includes('z'.repeat(TAB_TITLE_CHARS - 1) + '…'));
});

// A tab's ?query/#fragment carry session ids and search terms and don't help pick a
// tab. Truncation can't stand in for dropping them: a length cut keeps the FRONT.
test('buildTabsListing: a tab URL is reduced to origin + path before the model sees it', () => {
  const [line] = buildTabsListing([
    { title: 'Inbox', url: 'https://mail.example.com/u/0/inbox?token=s3cr3t&q=invoice#msg-9' },
  ]).split('\n');
  assert.equal(line, '0: "Inbox" — https://mail.example.com/u/0/inbox');
  assert.ok(!line.includes('s3cr3t'), 'the query string must never reach the prompt');
  assert.ok(!line.includes('msg-9'), 'nor the fragment');

  // Unparseable input still gets cut at the first ? or # rather than passed through.
  const [odd] = buildTabsListing([{ title: 'x', url: 'not a url?token=leak' }]).split('\n');
  assert.ok(!odd.includes('leak'));
});

test('system suffix carries the tabs listing when getTabs answers', async () => {
  const { controller, calls } = makeController(['chat'], { getTabs: async () => TABS });
  await controller.send('hi');
  assert.match(calls[0].system, /Other open browser tabs — scan one with \{"op":"scanTab","tab":N\}/);
  assert.match(calls[0].system, /0: "stencil\/bot\/assets at main"/);
});

test('no tabs → no tabs block, and a scanTab plan is a plan error', async () => {
  const { controller, calls } = makeController([
    JSON.stringify({ version: 1, reply: 'sure', actions: [{ op: 'scanTab', tab: 0 }], variants: [] }),
  ]);
  await assert.rejects(() => controller.send('scan the assets tab'), /no other open tabs/);
  assert.doesNotMatch(calls[0].system, /Other open browser tabs/);
});

test('scanTab-only plan: swaps the listing, notes it in history, auto-continues once', async () => {
  const SWAPPED = [{ kind: 'img', src: 'https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/description.jpg', w: 20, h: 20 }];
  let working = null;
  const { controller, calls } = makeController([
    JSON.stringify({ version: 1, reply: 'Scanning that tab.', actions: [{ op: 'scanTab', tab: 0 }], variants: [] }),
    'It lists 1 image.',
  ], {
    getTabs: async () => TABS,
    getListing: () => working || LISTING,
    scanTab: async (tab, index) => {
      assert.equal(tab.tabId, 11);
      assert.equal(index, 0);
      working = SWAPPED;
      return { ok: true, count: SWAPPED.length, title: tab.title };
    },
  });
  const result = await controller.send('look at the assets tab');
  assert.equal(calls.length, 2);   // exactly one continuation
  assert.deepEqual(result.cards, [{ kind: 'scanTab', index: 0, title: 'stencil/bot/assets at main', count: 1, ok: true }]);
  // The continuation round saw the SWAPPED listing in its suffix…
  assert.match(calls[1].system, /description\.jpg/);
  // …and the history explains the switch as a user-side note.
  const note = controller.history.find((m) => /Scanned open tab 0/.test(m.text || ''));
  assert.ok(note && note.role === 'user');
  assert.equal(result.continuation.reply, 'It lists 1 image.');
});

test('a failed scanTab keeps the turn alive: warning + failure card, no continuation', async () => {
  const { controller, calls } = makeController([
    JSON.stringify({ version: 1, reply: 'Scanning.', actions: [{ op: 'scanTab', tab: 1 }], variants: [] }),
  ], {
    getTabs: async () => TABS,
    scanTab: async () => ({ ok: false, error: 'this page can’t be scanned' }),
  });
  const result = await controller.send('scan the news tab');
  assert.equal(calls.length, 1);
  assert.ok(result.warnings.some((w) => /Could not scan tab 1/.test(w)));
  assert.deepEqual(result.cards, [{ kind: 'scanTab', index: 1, title: 'News', ok: false }]);
});
