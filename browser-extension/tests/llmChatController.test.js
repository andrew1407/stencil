// Tests for the extension chat controller (src/llm/chatController.js): the §8
// context listing (100-entry cap, name/alt truncation), the open.actions →
// launch-option translation table, the §7 image replay rule, and the bounded
// attach auto-continuation — driven with a scripted mock client + stub
// focus/open/attach capabilities (no chrome, no fetch).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { formatOfItem } from '../src/lib/filters.js';
import {
  LISTING_LIMIT, LISTING_NAME_CHARS, LISTING_ALT_CHARS,
  buildListing, listingKind, translateOpenActions, replayMessages, splitDataUrl,
  matchListingIndex, attachmentNote, createChatController, rejectForbidden,
} from '../src/llm/chatController.js';
import { LLM_SYSTEM_PROMPT } from '../src/llm/opPlan.js';

// ── Listing (contract §8) ──

test('listingKind maps scan records onto the §8 categories', () => {
  assert.equal(listingKind({ kind: 'img', src: 'x' }), 'img');
  assert.equal(listingKind({ kind: 'bg', src: 'x' }), 'background');
  assert.equal(listingKind({ kind: 'video', videoUrl: 'x' }), 'video');
  assert.equal(listingKind({ kind: 'img', src: 'x', poster: true }), 'poster');
  assert.equal(listingKind({ kind: 'img', src: 'x', meta: true }), 'icon');
});

test('buildListing lines carry index, kind, dims, format, basename, alt', () => {
  const items = [
    { kind: 'img', src: 'https://a.example/photos/cat.png', w: 800, h: 600, alt: 'A cat' },
    { kind: 'bg', src: 'https://a.example/tiles/bg.jpg', w: 0, h: 0, alt: '' },
    { kind: 'video', src: 'data:image/jpeg;base64,FRAME', videoUrl: 'https://a.example/v/clip.mp4', w: 1920, h: 1080, alt: 'video' },
    { kind: 'img', src: 'https://a.example/favicon.ico', w: 0, h: 0, alt: 'icon', meta: true },
  ];
  const lines = buildListing(items, { formatOfItem }).split('\n');
  assert.equal(lines[0], '0: img 800x600 png "cat.png" alt "A cat"');
  assert.equal(lines[1], '1: background jpg "bg.jpg"');            // no dims/alt when unknown
  assert.equal(lines[2], '2: video 1920x1080 mp4 "clip.mp4" alt "video"');   // video keys on its media URL
  assert.equal(lines[3], '3: icon ico "favicon.ico" alt "icon"');
});

test('buildListing truncates to 100 entries and notes the overflow', () => {
  const items = Array.from({ length: 105 }, (_, i) => ({ kind: 'img', src: `https://a.example/i${i}.png`, w: 0, h: 0 }));
  const lines = buildListing(items, { formatOfItem }).split('\n');
  assert.equal(lines.length, LISTING_LIMIT + 1);
  assert.equal(lines[99], '99: img png "i99.png"');
  assert.equal(lines[100], '(+5 more not listed)');
});

test('buildListing truncates long names and alt text', () => {
  const longName = 'x'.repeat(90) + '.png';
  const longAlt = 'y'.repeat(200);
  const [line] = buildListing(
    [{ kind: 'img', src: `https://a.example/${longName}`, w: 0, h: 0, alt: longAlt }],
    { formatOfItem },
  ).split('\n');
  const name = /"([^"]*)" alt "([^"]*)"$/.exec(line);
  assert.ok(name, line);
  assert.equal(name[1].length, LISTING_NAME_CHARS);
  assert.ok(name[1].endsWith('…'));
  assert.equal(name[2].length, LISTING_ALT_CHARS);
  assert.ok(name[2].endsWith('…'));
});

test('buildListing handles data: sources and in-page videos gracefully', () => {
  const lines = buildListing([
    { kind: 'img', src: 'data:image/png;base64,AAAA', w: 4, h: 4 },
    { kind: 'video', src: '', videoUrl: '', w: 0, h: 0 },
  ], { formatOfItem }).split('\n');
  assert.match(lines[0], /"\(inline data\)"/);
  assert.match(lines[1], /"\(in-page video\)"/);
});

// ── open.actions → launch options (contract §8 translation) ──

test('crop: % / px / bare / negative tokens resolve against the image dims', () => {
  const dims = { width: 1000, height: 500 };
  const { launch, warnings } = translateOpenActions(
    [{ op: 'crop', spec: { x1: '10%', x2: '-10%', y1: '50', y2: '400px' } }], dims);
  assert.deepEqual(warnings, []);
  // Launch crops leave in the canonical {w,h} wire spelling.
  assert.deepEqual(launch.crop, { x: 100, y: 50, w: 800, h: 350 });
});

test('crop: unspecified edges default to the full image; later crops keep earlier edges', () => {
  const dims = { width: 1000, height: 500 };
  const one = translateOpenActions([{ op: 'crop', spec: { x1: '200' } }], dims);
  assert.deepEqual(one.launch.crop, { x: 200, y: 0, w: 800, h: 500 });
  const two = translateOpenActions(
    [{ op: 'crop', spec: { x1: '200' } }, { op: 'crop', spec: { y2: '-100' } }], dims);
  assert.deepEqual(two.launch.crop, { x: 200, y: 0, w: 800, h: 400 });
});

test('crop: cm/in tokens cannot resolve in the extension → dropped with a warning', () => {
  const { launch, warnings } = translateOpenActions(
    [{ op: 'crop', spec: { x1: '2cm' } }], { width: 1000, height: 500 });
  assert.equal(launch.crop, undefined);
  assert.match(warnings[0], /cm\/in/);
});

test('crop: unknown image dimensions → dropped with a warning', () => {
  const { launch, warnings } = translateOpenActions([{ op: 'crop', spec: { x1: '10%' } }], {});
  assert.equal(launch.crop, undefined);
  assert.match(warnings[0], /dimensions/);
});

test('rotate has no launch-payload equivalent → dropped with a warning', () => {
  const { launch, warnings } = translateOpenActions(
    [{ op: 'rotate', dir: 'left', times: 2 }], { width: 10, height: 10 });
  assert.deepEqual(launch, {});
  assert.match(warnings[0], /rotate/i);
});

test('filter and layout fold into one layout payload (imageFilter/filterColor/lines)', () => {
  const lines = [{ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color: '#FFFF00' }];
  const { launch, warnings } = translateOpenActions([
    { op: 'filter', mode: 'custom', tint: '#12ab34' },
    { op: 'layout', lines },
  ], { width: 640, height: 480 });
  assert.deepEqual(warnings, []);
  assert.deepEqual(launch.layout, {
    lines, imageFilter: 'custom', filterColor: '#12ab34', imageWidth: 640, imageHeight: 480,
  });
});

test('a non-custom filter carries no filterColor; two layouts concatenate lines', () => {
  const l1 = [{ points: [{ x: 0, y: 0 }] }];
  const l2 = [{ points: [{ x: 1, y: 1 }] }];
  const { launch } = translateOpenActions([
    { op: 'layout', lines: l1 },
    { op: 'filter', mode: 'bw' },
    { op: 'layout', lines: l2 },
  ], { width: 10, height: 10 });
  assert.equal(launch.layout.imageFilter, 'bw');
  assert.equal(launch.layout.filterColor, undefined);
  assert.deepEqual(launch.layout.lines, [...l1, ...l2]);
});

test('page maps to the launch page slot with the canonical uppercase name', () => {
  const { launch } = translateOpenActions([{ op: 'page', format: 'a4' }], { width: 10, height: 10 });
  assert.deepEqual(launch.page, { size: 'A4' });
  assert.deepEqual(translateOpenActions([{ op: 'page', format: 'b10' }], {}).launch.page, { size: 'B10' });
});

// ── §7 image replay rule ──

test('replayMessages keeps the current turn images + only the most recent prior image', () => {
  const img = (d) => ({ mediaType: 'image/png', data: d });
  const history = [
    { role: 'user', text: 'a', images: [img('OLD')] },
    { role: 'assistant', text: 'r1' },
    { role: 'user', text: 'b', images: [img('P1'), img('P2')] },
    { role: 'assistant', text: 'r2' },
    { role: 'user', text: 'c', images: [img('NOW1'), img('NOW2')] },
  ];
  const out = replayMessages(history);
  assert.equal(out[0].images, undefined);                       // older turn → text-only
  assert.deepEqual(out[2].images, [img('P2')]);                 // most recent prior → last image only
  assert.deepEqual(out[4].images, [img('NOW1'), img('NOW2')]);  // current turn → all images
});

test('splitDataUrl parses the LlmImage wire shape', () => {
  assert.deepEqual(splitDataUrl('data:image/png;base64,AAAA'), { mediaType: 'image/png', data: 'AAAA' });
  assert.equal(splitDataUrl('https://not-a-data-url'), null);
});

// ── Controller: execution + bounded auto-continuation ──

const LISTING = [
  { kind: 'img', src: 'https://a.example/one.png', w: 10, h: 10, alt: '', name: 'one.png' },
  { kind: 'img', src: 'https://a.example/two.png', w: 20, h: 20, alt: 'second', name: 'two.png' },
  { kind: 'img', src: 'https://a.example/three.png', w: 30, h: 30, alt: '', name: 'three.png' },
];

// A scripted client: replies from the queue (last reply repeats), records calls.
const scriptedClient = (responses) => {
  const calls = [];
  const client = {
    chat: async ({ system, messages }) => {
      calls.push({ system, messages: JSON.parse(JSON.stringify(messages)) });
      return responses[Math.min(calls.length - 1, responses.length - 1)];
    },
  };
  return { calls, client };
};

const makeController = (responses, overrides = {}) => {
  const { calls, client } = scriptedClient(responses);
  const log = { focused: [], opened: [], attached: [] };
  const controller = createChatController({
    getClient: () => client,
    getListing: () => LISTING,
    formatOfItem,
    pageUrl: () => 'https://a.example/page',
    focusImage: async (i) => { log.focused.push(i); return true; },
    openImage: async (a) => { log.opened.push(a); return []; },
    attachImage: async (i) => { log.attached.push(i); return { mediaType: 'image/png', data: `IMG${i}` }; },
    ...overrides,
  });
  return { controller, calls, log };
};

const ATTACH_PLAN = JSON.stringify({ version: 1, reply: 'Let me look at them.', actions: [{ op: 'attach', images: [0, 2] }], variants: [] });

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

// ── §8 tabs listing + scanTab / pin ──
import { buildTabsListing, TABS_LIMIT, TAB_TITLE_CHARS } from '../src/llm/chatController.js';

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

// ── §8 panel-op widening: rescan / unpin / accent ──

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

// ── §13 forbidden ops: the executor-level tooth ──

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

// ── §10 clearChat: deferred to the END of the turn ──

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
