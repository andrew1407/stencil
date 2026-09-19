// The empty state both chat surfaces share (js/ui/chatView.js): one suggestion list, owned
// by renderChatLog, and a flyout that adds no duplicate panel ids.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { layout } from '../js/ui/layout.js';
import { assistantItemHtml } from '../js/ui/contextMenu.js';
import { CHAT_SUGGESTIONS, chatSuggestionsHtml } from '../js/ui/chatView.js';
import { chatViewSource } from './helpers/chatViewSource.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';
import { layoutWith } from './helpers/ctxAssistantRig.js';

// ── The empty state is one shared contract, not a per-surface hack ──
test('BOTH surfaces ship the SAME suggestion chips, from one list', () => {
  const markup = layoutWith({ provider: 'ollama', baseUrl: 'http://localhost:11434' });
  // The flyout is built by syncAssistant, so its markup comes from assistantItemHtml; the chips
  // are everything between the empty-state block and the composer, not the first </div>.
  const blockAfter = (m, id) => {
    const from = m.indexOf(id);
    const start = m.indexOf('class="chat-empty"', from);
    const end = m.indexOf('<textarea', start);
    return m.slice(start, end === -1 ? undefined : end);
  };
  const prompts = (html) => [...html.matchAll(/data-prompt="([^"]+)"/g)].map((m) => m[1]);
  const panel = prompts(blockAfter(markup, 'id="chat-transcript"'));
  const flyout = prompts(blockAfter(assistantItemHtml(), 'id="ctx-assist-transcript"'));
  assert.deepStrictEqual(flyout, panel, 'identical chips in the panel and the flyout');
  assert.deepStrictEqual(panel, CHAT_SUGGESTIONS.map((s) => s.prompt), 'and they come from the shared list');
  assert.ok(panel.length >= 4 && panel.includes('Make it sepia'));
  // Both templates interpolate the shared helper — neither hand-rolls its own chips.
  for (const [name, src] of [
    ['panel', readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8')],
    ['flyout', contextMenuSource()],
  ]) {
    assert.ok(src.includes('${chatSuggestionsHtml()}'), `${name} uses the shared chip markup`);
    assert.strictEqual(src.split('class="chat-suggest"').length - 1, 0, `${name} hand-rolls no chips`);
  }
  assert.ok(chatSuggestionsHtml().includes('data-prompt="Make it sepia"'));
});

test('renderChatLog OWNS the empty state: chips whenever the log is empty', () => {
  const view = chatViewSource();
  // One rule, in the renderer: a non-empty log means no chips, an empty one means chips (rebuilt
  // if the block was dropped), but only after the rows have left (restoreEmptyState).
  assert.ok(view.includes("if (log.length) transcript.querySelector('.chat-empty')?.remove();"));
  assert.ok(view.includes('restoreEmptyState(transcript, log, wiped);'));
  assert.ok(view.includes("if (!transcript.querySelector('.chat-empty')) transcript.prepend(chatEmptyState());"));
  // No per-surface restore hack left: neither surface passes its own empty-state node.
  for (const [name, src] of [
    ['panel', readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8')],
    ['flyout', contextMenuSource()],
  ]) {
    assert.strictEqual(src.split('emptyState').length - 1, 0, `${name} no longer owns an empty state`);
    // Chips are delegated on the (stable) transcript, so a rebuilt block stays clickable.
    assert.ok(src.includes('wireChatSuggestions(transcript, (prompt) => {'), `${name} delegates chip clicks`);
  }
});

test('the flyout reuses the panel chat classes and adds no duplicate panel ids', () => {
  const html = assistantItemHtml();
  for (const cls of ['chat-empty', 'chat-suggest', 'ctx-assist-transcript']) assert.ok(html.includes(cls), `${cls} used`);
  // The panel owns #chat-input / #chat-send / #chat-transcript — the menu must not
  // duplicate them (both live in the same document).
  for (const id of ['chat-input', 'chat-send', 'chat-transcript', 'chat-empty']) {
    assert.ok(!html.includes(`id="${id}"`), `no duplicate #${id}`);
  }
  assert.ok(html.includes('data-prompt="Make it sepia"'), 'suggestion chips carry their prompt');
  // The coordinator's composer layout: the sizer sits in its own column above the
  // textarea (so the pill centres on the input, not the whole row).
  assert.match(html, /<div class="ctx-assist-inputcol">\s*<div class="ctx-assist-sizer" id="ctx-assist-sizer"[^>]*><\/div>\s*<textarea id="ctx-assist-input"/);
});
