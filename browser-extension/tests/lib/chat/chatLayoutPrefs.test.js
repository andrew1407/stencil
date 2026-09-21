import { test } from 'node:test';
import assert from 'node:assert';
import { assistantSrc } from '../../helpers/sources.js';
import { readFileSync } from 'node:fs';

// ── "Swap message sides" (lib/layoutPrefs.js) — browser js/ui/chat/layoutPrefs.js
// parity. Deliberately in-memory, NOT persisted (user report: must not survive a reload
// or a reopened popup/side panel/DevTools panel) — each test imports the module fresh
// (a distinct query string) so it starts from a clean module-level variable, the same
// way a real page open would.

test('chatSide defaults to normal, and round-trips through setChatSide', async () => {
  const { chatSide, setChatSide, CHAT_SIDE_NORMAL, CHAT_SIDE_SWAPPED } =
    await import('../../../src/lib/chat/layoutPrefs.js?prefs1');
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL);
  setChatSide(CHAT_SIDE_SWAPPED);
  assert.strictEqual(chatSide(), CHAT_SIDE_SWAPPED);
  setChatSide(CHAT_SIDE_NORMAL);
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL);
  setChatSide('garbage');
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL);
});

// The core of the user report: a fresh module instance — what a reopened popup/side
// panel/DevTools panel actually gets — never inherits a PRIOR instance's swapped side.
test('the side is per-module-instance — a fresh import never inherits an earlier one\'s swap', async () => {
  const first = await import('../../../src/lib/chat/layoutPrefs.js?prefs-reopen-a');
  first.setChatSide(first.CHAT_SIDE_SWAPPED);
  assert.strictEqual(first.chatSide(), first.CHAT_SIDE_SWAPPED);

  const second = await import('../../../src/lib/chat/layoutPrefs.js?prefs-reopen-b');
  assert.strictEqual(second.chatSide(), second.CHAT_SIDE_NORMAL,
    'a fresh instance (a reopened popup/side panel/DevTools panel) starts at the default');
});

test('toggleChatSide flips — a same-session load agrees', async () => {
  const { chatSide, toggleChatSide, CHAT_SIDE_NORMAL, CHAT_SIDE_SWAPPED } =
    await import('../../../src/lib/chat/layoutPrefs.js?prefs3');
  assert.strictEqual(toggleChatSide(), CHAT_SIDE_SWAPPED);
  assert.strictEqual(chatSide(), CHAT_SIDE_SWAPPED);
  assert.strictEqual(toggleChatSide(), CHAT_SIDE_NORMAL);
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL);
});

test('applyChatSide stamps the class the CSS swap rules key off, only when swapped', async () => {
  const { applyChatSide, CHAT_SIDE_NORMAL, CHAT_SIDE_SWAPPED, CHAT_SWAPPED_CLASS } =
    await import('../../../src/lib/chat/layoutPrefs.js?prefs4');
  assert.strictEqual(CHAT_SWAPPED_CLASS, 'chat-swapped');
  const classes = new Set();
  const el = { classList: { toggle: (c, on) => { on ? classes.add(c) : classes.delete(c); } } };
  applyChatSide(el, CHAT_SIDE_SWAPPED);
  assert.ok(classes.has('chat-swapped'));
  applyChatSide(el, CHAT_SIDE_NORMAL);
  assert.ok(!classes.has('chat-swapped'));
  assert.doesNotThrow(() => applyChatSide(null, CHAT_SIDE_SWAPPED));
});

// ── The menu item itself: between Clear history and Settings, in all three surfaces ──
test('"Swap message sides" sits between Clear history and Settings in every host page', () => {
  for (const rel of ['../../../src/popup/popup.html', '../../../src/sidepanel/sidepanel.html', '../../../src/devtools/panel.html']) {
    const html = readFileSync(new URL(rel, import.meta.url), 'utf8');
    const clear = html.indexOf('id="chat-clear"');
    const swap = html.indexOf('id="chat-swap-sides"');
    const settings = html.indexOf('id="chat-open-options"');
    assert.ok(clear > -1 && swap > -1 && settings > -1, `${rel} is missing one of the three items`);
    assert.ok(clear < swap && swap < settings, `${rel}: swap must sit between clear and settings`);
    assert.match(html, /id="chat-swap-sides"[^>]*>Swap message sides</);
  }
});

test('assistant.js wires the icon and the click — no cross-page watch (each page is its own session)', () => {
  const js = assistantSrc();
  assert.match(js, /\['chat-swap-sides', 'swap'\]/, 'the menu item gets the swap glyph');
  assert.match(js, /applyChatSide\(transcriptEl\)/, 'applied on mount');
  assert.match(js, /applyChatSide\(transcriptEl, toggleChatSide\(\)\)/, 'click flips + re-applies');
  assert.doesNotMatch(js, /watchChatSide/, 'no persisted state left to watch another page for');
});
