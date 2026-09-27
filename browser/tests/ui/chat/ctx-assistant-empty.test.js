// The empty state both chat surfaces share (js/ui/chat/view.js): one suggestion list, owned
// by renderChatLog, and a flyout that adds no duplicate panel ids.
import { test, mock } from 'node:test';
import assert from 'node:assert';
import { layout } from '../../../js/ui/layout.js';
import { assistantItemHtml } from '../../../js/ui/contextMenu/contextMenu.js';
import { CHAT_SUGGESTIONS, chatSuggestionsHtml } from '../../../js/ui/chat/view.js';
import { layoutWith } from '../../helpers/ctxAssistantRig.js';
import { wireBothSurfaces } from '../../helpers/chatSurfacesRig.js';

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
  // Both templates carry the shared helper's markup verbatim, and no chip beside it.
  const chipCount = (html) => html.split('class="chat-suggest"').length - 1;
  for (const [name, html] of [['panel', blockAfter(markup, 'id="chat-transcript"')],
    ['flyout', blockAfter(assistantItemHtml(), 'id="ctx-assist-transcript"')]]) {
    assert.ok(html.includes(chatSuggestionsHtml()), `${name} uses the shared chip markup`);
    assert.strictEqual(chipCount(html), CHAT_SUGGESTIONS.length, `${name} hand-rolls no chips`);
  }
  assert.ok(chatSuggestionsHtml().includes('data-prompt="Make it sepia"'));
});

test('renderChatLog OWNS the empty state: chips whenever the log is empty', async () => {
  const s = await wireBothSurfaces();
  const empties = (t) => t.children.filter((c) => c.classList.contains('chat-empty'));
  const chip = (t) => empties(t)[0].children.find((c) => c.dataset.prompt === 'Make it sepia');
  for (const [name, surf] of [['panel', s.panel], ['flyout', s.flyout]]) {
    assert.strictEqual(empties(surf.transcript).length, 1, `${name}: an empty log paints the chips`);
  }
  const first = chip(s.panel.transcript);
  s.session.appendChatRow({ role: 'user', text: 'hi' });
  for (const surf of [s.panel, s.flyout]) assert.strictEqual(empties(surf.transcript).length, 0, 'a row drops them');
  mock.timers.enable({ apis: ['setTimeout'] });
  try {
    s.session.clearChatLog();
    for (const surf of [s.panel, s.flyout]) assert.strictEqual(empties(surf.transcript).length, 0, 'not while rows leave');
    mock.timers.tick(5000);
  } finally { mock.timers.reset(); }
  for (const [name, surf] of [['panel', s.panel], ['flyout', s.flyout]]) {
    // Rebuilt once the rows have left, by the renderer alone: one block, never a surface's second.
    assert.strictEqual(empties(surf.transcript).length, 1, `${name}: the chips come back, once`);
    // Delegated on the stable transcript, so the REBUILT block is clickable too.
    surf.input.value = '';
    surf.transcript.fire('click', { target: chip(surf.transcript) });
    assert.strictEqual(surf.input.value, 'Make it sepia', `${name} delegates chip clicks`);
  }
  assert.notStrictEqual(chip(s.panel.transcript), first, 'a fresh block, still wired');
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
