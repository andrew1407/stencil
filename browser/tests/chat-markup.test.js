import { test } from 'node:test';
import assert from 'node:assert';

// Layout transitively registers every ui component (chat panel + LLM settings modal
// included) and must compose their markup exactly once, appended after the original
// regions (the REGIONS order is load-bearing).
import { layout } from '../js/ui/layout.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;
const once = (id) => assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);

test('chat panel ids are present exactly once', () => {
  for (const id of [
    'chat-panel', 'chat-header', 'chat-title', 'chat-status-dot',
    'chat-dock-left-btn', 'chat-dock-top-btn', 'chat-dock-bottom-btn', 'chat-dock-right-btn', 'chat-float-btn',
    'chat-settings-btn', 'chat-close', 'chat-transcript', 'chat-empty', 'chat-attachments',
    'chat-attach-btn', 'chat-attach-input', 'chat-input', 'chat-send', 'chat-voice', 'chat-resizer',
    'chat-input-sizer',
  ]) once(id);
});

test('LLM settings modal ids are present exactly once', () => {
  for (const id of [
    'chat-settings-overlay', 'chat-settings-close', 'chat-provider',
    'chat-base-url-row', 'chat-base-url', 'chat-model', 'chat-api-key-row', 'chat-api-key',
    'chat-server-row', 'chat-server-select', 'chat-server-status-row', 'chat-server-status',
    'chat-cors-note', 'chat-save-chats',
  ]) once(id);
});

test('chat persistence opt-in (§12) is a checkbox that ships unchecked', () => {
  const i = markup.indexOf('id="chat-save-chats"');
  const tag = markup.slice(markup.lastIndexOf('<input', i), markup.indexOf('>', i) + 1);
  assert.ok(tag.includes('type="checkbox"'), 'saveChats is a checkbox');
  assert.ok(!tag.includes('checked'), 'ships OFF — persistence is an explicit opt-in');

  // §12.2: who can read a saved chat has to be VISIBLE at the toggle, not only in a title= — a server
  // project's chat carries that project's access.
  const note = markup.slice(markup.indexOf('id="chat-save-chats-note"'));
  const body = note.slice(0, note.indexOf('</div>'));
  assert.match(body, /shared with/, 'the sharing consequence is stated next to the toggle');
});

test('toolbar toggle + glyphs are composed in', () => {
  once('chat-btn');
  assert.ok(markup.includes('ic-sparkle'), 'sparkle glyph used');
  assert.ok(markup.includes('ic-send'), 'send glyph used');
});

test('provider select offers the three contract providers plus the local-only off switch', () => {
  const sel = markup.slice(markup.indexOf('id="chat-provider"'));
  const options = sel.slice(0, sel.indexOf('</select>'));
  for (const v of ['none', 'ollama', 'openai-compat', 'stencil-server']) {
    assert.ok(options.includes(`<option value="${v}"`), `provider option ${v} present`);
  }
});

test('chat regions append at the END of the body (after the install button)', () => {
  assert.ok(markup.indexOf('id="install-host"') < markup.indexOf('id="chat-panel"'), 'panel after install');
  assert.ok(markup.indexOf('id="chat-panel"') < markup.indexOf('id="chat-settings-overlay"'), 'modal after panel');
});

test('CORS help note for local providers is present', () => {
  assert.ok(markup.includes('OLLAMA_ORIGINS'), 'Ollama origins hint');
  assert.ok(markup.includes('enable CORS'), 'LM Studio CORS hint');
});

// ── Header ghosts + accent action row (send first, then media, then settings) ──
test('header buttons are chat-hbtn ghosts; composer is send + a … menu', () => {
  assert.strictEqual(count('class="chat-hbtn'), 6, '5 dock + close are header ghosts');
  // Only SEND and the … trigger stay inline; the rest live in the menu.
  assert.strictEqual(count('chat-abtn'), 2, 'send + the … trigger are the inline action buttons');
  const send = markup.indexOf('id="chat-send"');
  const more = markup.indexOf('id="chat-more-btn"');
  const menu = markup.indexOf('id="chat-more-menu"');
  assert.ok(send < more && more < menu, 'order: send, then the … trigger and its menu');
  for (const id of ['chat-attach-btn', 'chat-clear', 'chat-swap-sides', 'chat-settings-btn']) {
    assert.ok(markup.indexOf(`id="${id}"`) > menu, `${id} sits inside the … menu`);
  }
  // …in that order: swap sides sits between Clear history and Settings.
  assert.ok(markup.indexOf('id="chat-clear"') < markup.indexOf('id="chat-swap-sides"')
    && markup.indexOf('id="chat-swap-sides"') < markup.indexOf('id="chat-settings-btn"'));
  assert.strictEqual(count('chat-more-item'), 5, 'voice input + add image + clear history + swap sides + settings');
  assert.ok(markup.indexOf('id="chat-voice"') > menu && markup.indexOf('id="chat-voice"') < markup.indexOf('id="chat-attach-btn"'),
    'voice input is the first … item');
  assert.strictEqual(count('chat-title-text'), 1, 'truncatable title span present');
});

// ── One gear only, in the input row; actions grouped RIGHT of the input ──
test('input row: textarea first, then send · … (menu holds attach/clear/settings)', () => {
  const iInput = markup.indexOf('id="chat-input"');
  const iAttach = markup.indexOf('id="chat-attach-btn"');
  const iGear = markup.indexOf('id="chat-settings-btn"');
  const iSend = markup.indexOf('id="chat-send"');
  assert.ok(iInput < iSend && iSend < iAttach && iAttach < iGear, 'input stretches; send leads the action group');
  assert.strictEqual(count('chat-input-actions'), 1, 'right-aligned action group present');
  // The ONLY gear lives in the input row (inside the actions group, after the input).
  const header = markup.slice(markup.indexOf('id="chat-header"'), markup.indexOf('id="chat-transcript"'));
  assert.ok(!header.includes('chat-settings-btn'), 'no duplicate gear in the header');
  // The provider-status dot rides the … trigger — the menu's visible face now
  // that the gear itself lives inside the menu.
  const iMore = markup.indexOf('id="chat-more-btn"');
  const moreBtn = markup.slice(iMore, markup.indexOf('</button>', iMore));
  assert.ok(moreBtn.includes('id="chat-status-dot"'), 'status dot rides the … trigger');
});

test('send ships disabled (no text yet); empty state is a single subtle line', () => {
  assert.ok(markup.includes('id="chat-send" disabled'), 'send starts disabled');
  // Empty state = clickable prompt suggestions (data-prompt prefills the input).
  assert.ok(count('chat-suggest"') >= 3, 'at least three suggestion chips');
  assert.ok(markup.includes('data-prompt="Make it sepia"'), 'suggestion carries its prompt');
  assert.ok(markup.includes('data-prompt="3 variants: rotated · tinted · cropped"'), 'variants suggestion present');
  assert.strictEqual(count('chat-intro'), 0, 'the heavy intro card is gone');
});

test('edge drop zones are transient drag-time DOM — never in the static markup', () => {
  assert.strictEqual(count('chat-dock-zone'), 0);
});
