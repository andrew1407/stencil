// ── uiStrings.json drift guard ──────────────────────────────────────────────
// The asset is only real if the app RENDERS it: every string below is looked for in the
// composed markup (or in the module that exports it), so moving one back into a literal —
// or rewording either copy — fails here. The §12 disclosure and the compare tooltip are
// contract/UX text: they are pinned byte-for-byte on top of that.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import UI_STRINGS from '../js/config/uiStrings.json' with { type: 'json' };
import HOTKEY_DEFS from '../js/config/hotkeysConfig.json' with { type: 'json' };
import { layout } from '../js/ui/layout.js';
import {
  CHAT_SUGGESTIONS, chatSuggestionsHtml,
  SEND_TITLE, SEND_TITLE_PLAIN, VOICE_TITLE_LISTENING, VOICE_TITLE_PAUSED,
} from '../js/ui/chat/chatView.js';
import { WINDOWS } from '../js/console/stencilApi.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const markup = layout();

test('the chat suggestion chips come from the asset and reach the markup', () => {
  assert.deepEqual(CHAT_SUGGESTIONS,
    UI_STRINGS.chat.suggestions.map((s) => ({ prompt: s, label: s })));
  const html = chatSuggestionsHtml();
  for (const s of UI_STRINGS.chat.suggestions) {
    assert.ok(html.includes(`data-prompt="${s}"`), `chip "${s}" is offered`);
    assert.ok(markup.includes(s), `chip "${s}" is in the first paint`);
  }
  assert.equal(UI_STRINGS.chat.suggestions.length, 4);
});

test('the send / voice tooltips are the asset strings', () => {
  assert.equal(SEND_TITLE, UI_STRINGS.chat.sendTitle);
  assert.equal(SEND_TITLE_PLAIN, UI_STRINGS.chat.sendTitlePlain);
  assert.equal(VOICE_TITLE_LISTENING, UI_STRINGS.chat.voiceTitleListening);
  assert.equal(VOICE_TITLE_PAUSED, UI_STRINGS.chat.voiceTitlePaused);
  assert.ok(markup.includes(`data-title="${UI_STRINGS.chat.sendTitle}"`));
});

// llm-contract.md §12.2: who can read a saved chat must be visible AT the toggle.
test('the §12 save-chats disclosure is byte-identical and sits in its own div', () => {
  const { saveChatsNote, saveChatsTooltip } = UI_STRINGS.assistantSettings;
  const at = markup.indexOf('id="chat-save-chats-note"');
  assert.ok(at > -1, 'the note div is composed');
  const body = markup.slice(at, markup.indexOf('</div>', at));
  assert.ok(body.includes(saveChatsNote), 'the note renders the asset string, unchanged');
  assert.ok(saveChatsNote.includes('<strong>readable by everyone'), 'the consequence is emphasised');
  assert.ok(markup.includes(`data-title="${saveChatsTooltip}"`), 'and the tooltip too');
  assert.equal(saveChatsTooltip.length, 315);
  assert.equal(saveChatsTooltip.split('&#10;').length, 3, 'two blank-line breaks, as tipContent splits them');
});

test('the compare tooltip is the asset string, newline-joined for tipContent', () => {
  const tip = UI_STRINGS.toolbar.compareTooltip;
  assert.equal(tip.length, 233);
  assert.equal(tip.split('&#10;').length, 6, 'a heading, four modes, the peek hint');
  assert.ok(markup.includes(`data-title="${tip}"`));
});

test('the windows registry is the asset, and every row can actually be opened', () => {
  assert.deepEqual(WINDOWS, UI_STRINGS.windows);
  assert.equal(Object.isFrozen(WINDOWS), true);
  const hotkeyIds = new Set(HOTKEY_DEFS.map((h) => h.id));
  const keys = new Set();
  const names = new Set();
  for (const w of UI_STRINGS.windows) {
    assert.equal(keys.has(w.key), false, `${w.key} is declared twice`);
    keys.add(w.key);
    // Every name a caller may pass resolves to ONE window (windowNameKey's normalisation);
    // a row's own key and title routinely normalise alike, so dedupe within the row first.
    const own = new Set([w.key, w.title, ...(w.aliases || [])]
      .map((n) => n.toLowerCase().replace(/[^a-z0-9]+/g, '')));
    for (const norm of own) {
      assert.equal(names.has(norm), false, `"${norm}" collides with another window's name`);
      names.add(norm);
    }
    assert.ok(hotkeyIds.has(w.hotkey), `${w.key}: "${w.hotkey}" is not a hotkey id`);
    assert.ok(markup.includes(`id="${w.overlay}"`), `${w.key}: overlay ${w.overlay} is composed`);
    for (const id of [w.opener].flat())
      assert.ok(markup.includes(`id="${id}"`), `${w.key}: opener ${id} is composed`);
  }
  assert.equal(UI_STRINGS.windows.length, 13);
});

test('every §10 dialog button is a window opener, and the module reads the asset', () => {
  const openers = new Set(UI_STRINGS.windows.flatMap((w) => [w.opener].flat()));
  for (const [name, id] of Object.entries(UI_STRINGS.dialogButtonIds)) {
    assert.ok(openers.has(id), `dialog "${name}" -> ${id} is not a window opener`);
    assert.ok(UI_STRINGS.windows.some((w) => w.key === name || (w.aliases || []).includes(name)),
      `dialog "${name}" names no window`);
  }
  const src = readFileSync(resolve(ROOT, 'js/llm/adapters/dialog.js'), 'utf8');
  assert.ok(src.includes('const DIALOG_BUTTON_IDS = UI_STRINGS.dialogButtonIds;'));
});
