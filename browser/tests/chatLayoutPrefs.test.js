import { test } from 'node:test';
import assert from 'node:assert';

// ── "Swap message sides" (chatLayoutPrefs.js + chatView.js wireChatSideToggle) ──
// Deliberately in-memory, NOT persisted (user report: must not survive a reload or
// a reopened tab) — each test imports the module fresh (a distinct query string)
// so it starts from a clean module-level variable, the same way a real page load
// would.

test('chatSide defaults to normal, and round-trips through setChatSide', async () => {
  const { chatSide, setChatSide, CHAT_SIDE_NORMAL, CHAT_SIDE_SWAPPED } =
    await import('../js/ui/chatLayoutPrefs.js?prefs1');
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL, 'nothing set yet — the default');
  setChatSide(CHAT_SIDE_SWAPPED);
  assert.strictEqual(chatSide(), CHAT_SIDE_SWAPPED);
  setChatSide(CHAT_SIDE_NORMAL);
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL);
  // Anything else normalizes to the default, never thrown.
  setChatSide('garbage');
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL);
});

// The core of the user report: a fresh module instance — what a page reload or a
// reopened tab actually gets — never inherits a PRIOR instance's swapped side.
test('the side is per-module-instance — a fresh import never inherits an earlier one\'s swap', async () => {
  const first = await import('../js/ui/chatLayoutPrefs.js?prefs-reload-a');
  first.setChatSide(first.CHAT_SIDE_SWAPPED);
  assert.strictEqual(first.chatSide(), first.CHAT_SIDE_SWAPPED);

  const second = await import('../js/ui/chatLayoutPrefs.js?prefs-reload-b');
  assert.strictEqual(second.chatSide(), second.CHAT_SIDE_NORMAL,
    'a fresh instance (page reload / reopened tab) starts at the default, not the prior tab\'s swap');
});

test('toggleChatSide flips — a same-session load agrees', async () => {
  const { chatSide, toggleChatSide, CHAT_SIDE_NORMAL, CHAT_SIDE_SWAPPED } =
    await import('../js/ui/chatLayoutPrefs.js?prefs3');
  assert.strictEqual(toggleChatSide(), CHAT_SIDE_SWAPPED);
  assert.strictEqual(chatSide(), CHAT_SIDE_SWAPPED, 'set, not just returned');
  assert.strictEqual(toggleChatSide(), CHAT_SIDE_NORMAL);
  assert.strictEqual(chatSide(), CHAT_SIDE_NORMAL);
});

test('applyChatSide stamps the class the CSS swap rules key off, only when swapped', async () => {
  const { applyChatSide, CHAT_SIDE_NORMAL, CHAT_SIDE_SWAPPED, CHAT_SWAPPED_CLASS } =
    await import('../js/ui/chatLayoutPrefs.js?prefs4');
  assert.strictEqual(CHAT_SWAPPED_CLASS, 'chat-swapped');
  const classes = new Set();
  const el = { classList: { toggle: (c, on) => { on ? classes.add(c) : classes.delete(c); } } };
  applyChatSide(el, CHAT_SIDE_SWAPPED);
  assert.ok(classes.has('chat-swapped'));
  applyChatSide(el, CHAT_SIDE_NORMAL);
  assert.ok(!classes.has('chat-swapped'));
  // A null element (surface not mounted) is a no-op, never a throw.
  assert.doesNotThrow(() => applyChatSide(null, CHAT_SIDE_SWAPPED));
});

// ── wireChatSideToggle: applies on mount, flips on click ──
test('wireChatSideToggle applies the current side on mount and toggles on click', async () => {
  // chatView.js imports chatLayoutPrefs.js by its OWN unqueried specifier, so reach that exact module instance
  // here: a `?prefsN` copy is one wireChatSideToggle never sees.
  const prefs = await import('../js/ui/chatLayoutPrefs.js');
  prefs.setChatSide(prefs.CHAT_SIDE_SWAPPED);   // mounting while already swapped
  const { wireChatSideToggle } = await import('../js/ui/chatView.js?prefs-wire');

  const classes = new Set();
  const transcript = { classList: { toggle: (c, on) => { on ? classes.add(c) : classes.delete(c); } } };
  const listeners = {};
  const btn = { addEventListener: (t, fn) => { listeners[t] = fn; } };
  const doc = { getElementById: (id) => (id === 'chat-swap-sides' ? btn : null) };

  wireChatSideToggle('chat', transcript, doc);
  assert.ok(classes.has('chat-swapped'), 'mounted already-swapped — the class must be there from the start');

  listeners.click();
  assert.ok(!classes.has('chat-swapped'), 'one click flips it back to normal');
  listeners.click();
  assert.ok(classes.has('chat-swapped'), 'and back again');
});

test('wireChatSideToggle is a no-op with no transcript, and tolerates a missing button', async () => {
  const { wireChatSideToggle } = await import('../js/ui/chatView.js?prefs-wire2');
  assert.doesNotThrow(() => wireChatSideToggle('chat', null, { getElementById: () => null }));
  const transcript = { classList: { toggle: () => {} } };
  assert.doesNotThrow(() => wireChatSideToggle('chat', transcript, { getElementById: () => null }));
});

// ── The menu item itself: chatComposerActionsHtml, between Clear and Settings ──
test('the composer menu carries "Swap message sides" between Clear history and Settings', async () => {
  const { chatComposerActionsHtml } = await import('../js/ui/chatView.js?prefs-markup');
  const html = chatComposerActionsHtml({ prefix: 'chat', actionsClass: 'x', gearClass: 'y' });
  const clear = html.indexOf('id="chat-clear"');
  const swap = html.indexOf('id="chat-swap-sides"');
  const settings = html.indexOf('id="chat-settings-btn"');
  assert.ok(clear > -1 && swap > -1 && settings > -1);
  assert.ok(clear < swap && swap < settings, 'swap sits BETWEEN clear and settings, as asked');
  assert.match(html, /id="chat-swap-sides"[^>]*>.*?Swap message sides/s);
});
