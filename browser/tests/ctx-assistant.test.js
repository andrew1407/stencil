import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// The context menu's "Assistant ▸" entry: an ordinary submenu parent whose FLYOUT is
// a compact chat on the SAME conversation as the panel. The markup is gated on the
// configured provider, so these tests drive layout() with a stubbed localStorage to
// see both worlds.
import { layout } from '../js/ui/layout.js';
import { assistantEnabled, assistantItemHtml } from '../js/ui/contextMenu.js';
import { isTouchLike, TOUCH_MEDIA } from '../js/utils.js';
import {
  CHAT_SUGGESTIONS, chatSuggestionsHtml, chatDropCueHtml,
} from '../js/ui/chatView.js';
import {
  sharedChatController, peekChatController, forgetChatController,
  replyWithWarnings, unreachableText, describeChatError, runChatTurn,
  queueAttachments, cacheProbe, cachedProbe, forgetProbe, probeStatusClass, PROBE_TTL_MS,
  chatLog, onChatLog, appendChatRow, updateChatRow, clearChatLog, resetChatLog,
  clearSharedConversation, runLoggedChatTurn, attachmentPreviews,
} from '../js/llm/chatSession.js';
import { buildChatDoc, rowsToMessages } from '../js/llm/chatStore.js';
import { fileNameForUrl } from '../js/core/dragImageUrl.js';
import { scatterGridFor, SCATTER_TILE_BUDGET, SCATTER_MAX_ROWS } from '../js/ui/motion.js';
import { LlmError } from '../js/llm/llmClient.js';

const ASSIST_IDS = [
  'ctx-assist-menu', 'ctx-assist-sub', 'ctx-assist', 'ctx-assist-transcript',
  'ctx-assist-attachments', 'ctx-assist-sizer', 'ctx-assist-input', 'ctx-assist-send',
  'ctx-assist-attach-btn', 'ctx-assist-attach-input', 'ctx-assist-settings-btn', 'ctx-assist-status-dot',
];

// layout() reads the saved LLM settings each call — swap them for one composition.
const layoutWith = (settings) => {
  const saved = globalThis.localStorage;
  globalThis.localStorage = {
    getItem: (k) => (k === 'drawingApp_llmSettings' ? JSON.stringify(settings) : null),
    setItem: () => {},
  };
  try { return layout(); } finally {
    if (saved === undefined) delete globalThis.localStorage; else globalThis.localStorage = saved;
  }
};

// ── Presence / absence ──
test('the Assistant entry markup carries each id once, above Start Drawing', () => {
  const html = assistantItemHtml();
  for (const id of ASSIST_IDS) {
    assert.strictEqual(html.split(`id="${id}"`).length - 1, 1, `id="${id}" appears exactly once`);
  }
  // Same placeholder wording as the panel, and Enter/Shift+Enter documented in it.
  assert.ok(html.includes('placeholder="Ask the assistant… (Enter sends, Shift+Enter newline)"'));
  // The entry is built by syncAssistant, directly ABOVE Start Drawing (no separator
  // of its own — the layoutWith test below pins that gating changes nothing else).
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  assert.ok(src.includes("const anchor = document.getElementById('ctx-draw-toggle');"), 'anchored to Start Drawing');
  assert.ok(src.includes("anchor.insertAdjacentHTML('beforebegin', assistantItemHtml());"), 'inserted immediately above it');
  // Send ships disabled (nothing typed yet) and becomes Stop at runtime.
  assert.ok(html.includes('id="ctx-assist-send" disabled'));
});

// ── The regression that broke the classic flyouts: the chat must be a SUBMENU, and
// the root menu must never be re-clamped/resized while it is open (moving it under a
// stationary cursor fires mouseleave on the hovered item and kills its flyout). ──
test('the Assistant entry is a submenu PARENT, structured like Style / Image Filter', () => {
  const html = assistantItemHtml();
  // A .ctx-item with a caret and a nested .ctx-sub — the exact shape the hover
  // wiring keys on (`:scope > .ctx-item` + `:scope > .ctx-sub`).
  assert.match(html, /<div class="ctx-item" id="ctx-assist-menu">/);
  assert.ok(html.includes('class="ctx-arrow"'), 'carries the ▸ caret like the other parents');
  assert.match(html, /<div class="ctx-sub ctx-assist-sub" id="ctx-assist-sub">/);
  assert.ok(html.indexOf('ctx-arrow') < html.indexOf('ctx-assist-sub'), 'caret precedes the flyout, as in the others');
  // The chat lives INSIDE the flyout — never inline in the menu body.
  const flyout = html.slice(html.indexOf('id="ctx-assist-sub"'));
  for (const id of ['ctx-assist-transcript', 'ctx-assist-input', 'ctx-assist-send']) {
    assert.ok(flyout.includes(`id="${id}"`), `${id} lives in the flyout`);
  }
  assert.strictEqual(html.split('ctx-assist-section').length - 1, 0, 'the old inline section is gone');
  // It carries NO separator of its own — gating it off can't leave one dangling.
  assert.strictEqual(html.split('class="ctx-sep"').length - 1, 0, 'no separator ships with the entry');
});

test('the root menu is never re-positioned/observed while open (submenu-killer guard)', () => {
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // A ResizeObserver on the MENU re-clamps it as the chat grows → the hovered item
  // slides out from under the cursor → mouseleave → the flyout closes. Only the
  // FLYOUTS may be observed.
  assert.ok(!/ResizeObserver\([^)]*\)[\s\S]{0,80}\.observe\(menu\)/.test(src), 'no ResizeObserver on #ctx-menu');
  assert.strictEqual(src.split('.observe(sub)').length - 1, 1, 'exactly one observer, on the submenu');
  // placeMenu runs once per open, from openAt, with the click point.
  assert.strictEqual(src.split('placeMenu').length - 1, 2, 'defined once, called once');
  assert.ok(src.includes('placeMenu(x, y);'), 'called from openAt with the anchor');
  // Every submenu parent (incl. a late-built Assistant) goes through one wiring path.
  assert.ok(src.includes('const wireSubmenu = (item, sub) => {'));
  assert.ok(src.includes('if (item && sub) wireSubmenu(item, sub);'), 'the built-later Assistant is wired like the rest');
});

test('flyouts survive the menu moving under a stationary cursor', () => {
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // 1. A flyout placed while the entry pop still holds a transform anchors to the MENU
  //    (transformed ancestor = containing block for position:fixed) — finish the pop
  //    before measuring. animations.css documents the same hazard.
  assert.ok(src.includes('for (const a of menu.getAnimations?.() || []) a.finish();'), 'entry pop is settled before placing a flyout');
  assert.ok(src.indexOf('a.finish()') < src.indexOf("sub.style.left = '-9999px'"), 'settled BEFORE the measurement');
  const anims = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.ok(/#ctx-menu\.ctx-open \{[^}]*animation: menuPop[^}]*\}/.test(anims), 'the pop is still there…');
  assert.ok(!/#ctx-menu\.ctx-open \{[^}]*animation: menuPop[^;]*both/.test(anims), '…and still not `both`-filled');
  // 2. The pop moves items under a still cursor, which fires SYNTHETIC boundary events.
  //    Hover decisions ignore them: no pointer motion ⇒ no hover state change.
  assert.match(src, /const pointerIdle = \(\) => !!subShownPointer/);
  assert.ok(src.includes('subShownPointer = { ...lastPointer };'), 'the placement records the pointer');
  assert.ok(src.includes("for (const type of ['mousemove', 'mouseover', 'mouseout'])"),
    'pointer tracking covers the events that PRECEDE mouseenter/mouseleave');
  assert.ok(src.includes('if (pointerIdle() && activeSub && activeSub !== sub) return;'), 'no flyout stealing without motion');
  assert.ok(src.includes('if (!activeSub || keepSubOpen(activeSub) || pointerIdle()) return;'), 'no closing without motion');
  assert.ok(src.includes('positionSub(item, sub);   // the item moved, not the user — follow it'));
});

test('the menu pops out of the click point, and Escape (consumed) closes it', () => {
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // placeMenu stamps transform-origin from the click point vs the PLACED (clamped) box.
  assert.ok(src.includes('menuPopOrigin(x, y, { left, top, width: mw, height: mh })'),
    'the origin is the click point relative to the clamped position');
  const anims = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.match(anims, /@keyframes menuPop \{ from \{ opacity: 0; transform: scale\(0\.62\); \} to \{ opacity: 1; transform: none; \} \}/,
    'the pop scales up from the origin');
  assert.match(anims, /prefers-reduced-motion: reduce\) \{\s*#ctx-menu\.ctx-open, \.chat-row-menu \{ animation: none; \}/,
    'both menus opt out under prefers-reduced-motion');
  // Escape: capture phase, consumed only while OPEN — and a chat row menu floating
  // on top goes first (its own capture closer swallows the key).
  assert.match(src,
    /if \(e\.code !== 'Escape' \|\| !menuIsOpen\(\)\) return;\s*if \(chatRowMenuOpen\(\)\) return;\s*e\.stopPropagation\(\);\s*closeMenu\(\);\s*\}, true\);/,
    'Escape closes the open menu without leaking to other app handlers');
});

test('assistantEnabled gates on the provider only', () => {
  assert.strictEqual(assistantEnabled({ provider: 'none' }), false);
  assert.strictEqual(assistantEnabled(null), false);
  assert.strictEqual(assistantEnabled(undefined), false);
  for (const p of ['ollama', 'openai-compat', 'stencil-server']) {
    assert.strictEqual(assistantEnabled({ provider: p }), true, `${p} enables the entry`);
  }
  // A configured-but-unreachable provider still gets the entry (the failure shows
  // up in the reply, like the panel) — the gate never probes the endpoint.
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  assert.ok(src.includes('const on = assistantEnabled(loadLlmSettings());'), 'syncAssistant gates on the saved settings');
});

test('the assistant entry never touches the static menu markup (built by syncAssistant)', () => {
  // The static menu is byte-identical whatever the provider — the entry is built at
  // wire()/open time, so gating it on/off can never disturb the original menu.
  const off = layoutWith({ provider: 'none' });
  const on = layoutWith({ provider: 'ollama', baseUrl: 'http://localhost:11434' });
  assert.strictEqual(on, off, 'static markup is provider-independent');
  for (const id of ASSIST_IDS) assert.ok(!off.includes(`id="${id}"`), `${id} absent from the static markup`);
  const seps = (m) => m.split('class="ctx-sep"').length - 1;
  // Anchored on the LAST item of the view group (fullscreen — Fit to Window now leads the
  // whole menu), so the slice holds only the boundary, not the Image/Layout submenu's own
  // internal separator.
  const gap = (m) => m.slice(m.indexOf('id="ctx-fullscreen"'), m.indexOf('id="ctx-draw-toggle"'));
  assert.strictEqual(seps(gap(off)), 1, 'one separator between the two groups');
  // …and the built entry carries no separator of its own, so nothing can dangle.
  assert.strictEqual(assistantItemHtml().split('class="ctx-sep"').length - 1, 0);
});

// ── Desktop = flyout chat · phone / touch = plain item that opens the panel ──
test('plain mode rides the app-wide touch rule (utils.js), not a private query', () => {
  const mm = (matches) => (q) => ({ matches: q === TOUCH_MEDIA && matches });
  assert.strictEqual(isTouchLike(mm(false)), false, 'desktop → submenu flyout');
  assert.strictEqual(isTouchLike(mm(true)), true, 'phone width / coarse pointer → plain item');
  // The query is the panel's own phone breakpoint plus the no-hover case.
  assert.ok(TOUCH_MEDIA.includes('(max-width: 680px)'), 'same phone breakpoint as the chat modal');
  assert.ok(TOUCH_MEDIA.includes('(hover: none) and (pointer: coarse)'), 'no hover ⇒ no flyout');
  // No matchMedia at all (Node, old engines) → desktop behaviour, never a dead item.
  assert.strictEqual(isTouchLike(null), false);
  assert.strictEqual(isTouchLike(() => { throw new Error('bad query'); }), false);
  // The menu consults the shared helper — no alias, no duplicate media string.
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  assert.ok(src.includes('const plain = isTouchLike();'));
  assert.ok(!src.includes('(max-width: 680px)'), 'no private copy of the breakpoint');
});

test('plain mode is applied per open and hands over to the chat panel', () => {
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // Decided at open time (syncAssistant runs from openAt) and on resize while open.
  assert.ok(src.includes('const plain = isTouchLike();'));
  assert.ok(src.includes("item.dataset.noSub = plain ? '1' : '0';"));
  assert.ok(src.includes("item.classList.toggle('ctx-assist-plain', plain);"));
  assert.ok(src.includes("window.addEventListener('resize', () => { if (menuIsOpen()) syncAssistant(); });"));
  // Hover can't open a flyout in plain mode; a click closes the menu and opens the panel.
  assert.ok(src.includes("if (item.dataset.noSub === '1') { closeActiveSub(); return; }"));
  assert.match(src, /if \(item\.dataset\.noSub === '1'\) \{[\s\S]{0,160}closeMenu\(\);[\s\S]{0,80}openChatPanel\(\);/);
  // The panel is opened through the shared facade (same conversation), with a fallback.
  assert.ok(src.includes("if (typeof app?.chat?.open === 'function') app.chat.open();"));
  assert.ok(src.includes("document.getElementById('chat-btn')?.click();"));
  // CSS drops the caret and hard-blocks the flyout in plain mode.
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.ok(css.includes('.ctx-assist-plain .ctx-arrow { display: none; }'));
  assert.ok(css.includes('.ctx-assist-plain > .ctx-sub { display: none !important; }'));
});

// ── The composer carries the panel's whole action row ──
test('composer action row: send + a … menu holding attach · clear · settings', () => {
  const html = assistantItemHtml();
  const iInput = html.indexOf('id="ctx-assist-input"');
  const iSend = html.indexOf('id="ctx-assist-send"');
  const iAttach = html.indexOf('id="ctx-assist-attach-btn"');
  const iGear = html.indexOf('id="ctx-assist-settings-btn"');
  assert.ok(iInput < iSend && iSend < iAttach && iAttach < iGear, 'input, then send, then the menu items (panel order)');
  const iMore = html.indexOf('id="ctx-assist-more-btn"');
  assert.ok(iSend < iMore && iMore < iAttach, 'the … trigger sits between send and the menu items');
  // Same glyphs as the panel's row, and the gear carries the provider-status dot.
  assert.ok(html.includes('ic-send') && html.includes('ic-image') && html.includes('ic-gear'), 'panel glyphs');
  const more = html.slice(iMore, html.indexOf('</button>', iMore));
  assert.ok(more.includes('id="ctx-assist-status-dot"') && more.includes('conn-status-connecting'),
    'status dot rides the … trigger, amber until probed');
  // One file input, images + videos, multiple, hidden.
  assert.ok(html.includes('accept="image/*,video/*"') && html.includes('multiple'), 'the panel\'s picker filter');
  // Only send + the … trigger stay inline, both the same size (no bespoke ones).
  assert.strictEqual(html.split('ctx-assist-abtn').length - 1, 2, 'two identically-sized inline buttons');
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.ok(css.includes('.chat-abtn, .ctx-assist-abtn { width: 34px; height: 34px;'), 'the panel\'s 34px treatment, shared');
  // The flyout adds NO disabled treatment of its own: send inherits the app-wide
  // `button:disabled` styling, exactly like the panel's send does (verified identical
  // at runtime), so the two surfaces can't drift.
  assert.strictEqual(css.split('.ctx-assist-abtn:disabled').length - 1, 0, 'no bespoke disabled rule');
  assert.ok(readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8').includes('button:disabled,'),
    'the shared disabled palette is what both surfaces use');
  assert.ok(css.includes('.chat-config-btn, .ctx-assist-config { position: relative; }'), 'gear hosts the dot badge');
  const row = css.slice(css.indexOf('.ctx-assist-actions {'), css.indexOf('}', css.indexOf('.ctx-assist-actions {')));
  assert.ok(row.includes('flex-wrap: nowrap'), 'the action row never wraps');
  // The panel keeps its own ids — these must not collide.
  for (const id of ['chat-attach-btn', 'chat-attach-input', 'chat-settings-btn', 'chat-status-dot', 'chat-attachments']) {
    assert.ok(!html.includes(`id="${id}"`), `no duplicate #${id}`);
  }
});

test('attach queues into the shared controller; the gear opens the one settings modal', () => {
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // The picker wiring is the SHARED composer helper (chatView.js), fed by the shared
  // queueing helper on the SHARED controller, then both rows repaint.
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.ok(view.includes("attachBtn.addEventListener('click', () => attachInput.click());"), 'picker wiring lives in the shared composer');
  assert.ok(src.includes('wireChatComposer({ input, sendBtn, attachBtn, attachInput }'), 'the flyout wires through it');
  assert.match(src, /await queueAttachments\(sharedChatController\(app\),\s*files,/);
  assert.ok(src.includes('notifyAttachmentsChanged();'), 'the panel row repaints too');
  assert.ok(src.includes('window.addEventListener(CHAT_ATTACHMENTS_EVENT, renderAttachments);'), 'and this one listens back');
  assert.ok(view.includes('attachBtn.disabled = sending || attachFull;'), 'attach pauses mid-turn (and at the queue cap) — the shared control sync');
  // Gear → its own rect is captured (this popup is about to hide it) and handed to
  // the ONE settings modal directly, so it flies from THIS gear, not the panel's.
  assert.match(src, /gearBtn\.addEventListener\('click', \(\) => \{[\s\S]*?const rect = gearBtn\.getBoundingClientRect\(\);/);
  assert.match(src, /document\.getElementById\('chat-settings-overlay'\)\?\.__stencilModal\?\.open\(rect\);/);
  // Dot: the shared probe cache first, a probe only if nothing fresh is known.
  assert.ok(src.includes('const known = cachedProbe(settings);'));
  assert.ok(src.includes('.then((probe) => { cacheProbe(settings, probe); setDot(probe); })'));
});

test('queueAttachments adds every file and reports failures without losing the rest', async () => {
  const added = [];
  const controller = {
    attachments: added,
    async addAttachment(f) {
      if (f.name === 'bad.txt') throw new Error(`Not an image or video (got "${f.type}")`);
      added.push(f.name);
      return f;
    },
  };
  const errs = [];
  const n = await queueAttachments(controller, [{ name: 'a.png' }, { name: 'bad.txt', type: 'text/plain' }, { name: 'b.mp4' }],
    (err, file) => errs.push(`${file.name}: ${err.message}`));
  assert.strictEqual(n, 2, 'both good files landed');
  assert.deepStrictEqual(added, ['a.png', 'b.mp4']);
  assert.deepStrictEqual(errs, ['bad.txt: Not an image or video (got "text/plain")']);
  assert.strictEqual(await queueAttachments(controller, [], () => {}), 0);
});

test('the provider probe is cached per settings and expires, and maps to the dot class', () => {
  forgetProbe();
  const s = { provider: 'ollama', baseUrl: 'http://localhost:11434', model: '', serverUrl: '' };
  assert.strictEqual(cachedProbe(s), null, 'nothing known yet');
  const probe = { ok: true, provider: 'ollama', url: s.baseUrl };
  cacheProbe(s, probe);
  assert.strictEqual(cachedProbe(s), probe, 'the other surface reads it for free');
  // A different endpoint/model is a different probe — never shown for the new settings.
  assert.strictEqual(cachedProbe({ ...s, baseUrl: 'http://other:11434' }), null);
  assert.strictEqual(cachedProbe({ ...s, model: 'llava' }), null);
  // …and it goes stale.
  assert.strictEqual(cachedProbe(s, Date.now() + PROBE_TTL_MS + 1), null);
  // Dot classes (they match the .conn-status palette used by the panel).
  assert.strictEqual(probeStatusClass(null), 'connecting');
  assert.strictEqual(probeStatusClass({ ok: true }), 'connected');
  assert.strictEqual(probeStatusClass({ ok: false }), 'error');
  forgetProbe();
});

// ── The empty state is one shared contract, not a per-surface hack ──
test('BOTH surfaces ship the SAME suggestion chips, from one list', () => {
  const markup = layoutWith({ provider: 'ollama', baseUrl: 'http://localhost:11434' });
  // Pull each surface's empty-state block out (the flyout is built by syncAssistant,
  // so its markup comes from assistantItemHtml, not the composed body).
  // From the empty-state block to the composer: the chips are everything in between.
  // (Not "to the first </div>" — that is now the drop hint's, which precedes them.)
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
    ['flyout', readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8')],
  ]) {
    assert.ok(src.includes('${chatSuggestionsHtml()}'), `${name} uses the shared chip markup`);
    assert.strictEqual(src.split('class="chat-suggest"').length - 1, 0, `${name} hand-rolls no chips`);
  }
  assert.ok(chatSuggestionsHtml().includes('data-prompt="Make it sepia"'));
});

test('renderChatLog OWNS the empty state: chips whenever the log is empty', () => {
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  // One rule, in the renderer: non-empty log ⇒ no chips; empty log ⇒ chips (rebuilt if
  // the block was already dropped — that is what brings them back after Clear), but
  // only AFTER the rows have finished leaving (restoreEmptyState).
  assert.ok(view.includes("if (log.length) transcript.querySelector('.chat-empty')?.remove();"));
  assert.ok(view.includes('restoreEmptyState(transcript, log, wiped);'));
  assert.ok(view.includes("if (!transcript.querySelector('.chat-empty')) transcript.prepend(chatEmptyState());"));
  // No per-surface restore hack left: neither surface passes its own empty-state node.
  for (const [name, src] of [
    ['panel', readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8')],
    ['flyout', readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8')],
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

// ── One controller for the whole app: the panel and the menu share the history ──
const recordingController = () => ({
  history: [],
  calls: [],
  async send(text, opts) {
    this.calls.push({ text, opts });
    this.history.push({ role: 'user', text });
    this.history.push({ role: 'assistant', text: 'ok' });
    return { reply: `did ${text}`, warnings: [], results: [] };
  },
});

test('sharedChatController memoizes ONE controller per app (panel + menu share it)', () => {
  const app = {};
  let built = 0;
  const create = () => { built++; return recordingController(); };
  assert.strictEqual(peekChatController(app), null, 'nothing is created until first use');
  const fromPanel = sharedChatController(app, { create });
  const fromMenu = sharedChatController(app, { create });
  assert.strictEqual(fromPanel, fromMenu, 'the menu gets the panel\'s controller');
  assert.strictEqual(built, 1, 'built exactly once');
  assert.strictEqual(peekChatController(app), fromPanel);
  // A different app (another editor instance) gets its own.
  const other = sharedChatController({}, { create });
  assert.notStrictEqual(other, fromPanel);
  assert.strictEqual(built, 2);
  forgetChatController(app);
  assert.strictEqual(peekChatController(app), null);
});

test('the injected clearChatConversation capability: confirm-gated shared clear', async () => {
  resetChatLog();
  let allow = false;
  const confirms = [];
  const app = { confirm: async (msg, opts) => { confirms.push({ msg, opts }); return allow; } };
  const cleared = [];
  let captured;
  // Capture the REAL capability closures sharedChatController injects.
  const create = (opts) => { captured = opts; return { clearConversation: () => cleared.push(1) }; };
  sharedChatController(app, { create });
  appendChatRow({ role: 'user', text: 'hi' });

  // Declined: the §10 note; the transcript and the controller stay untouched.
  assert.strictEqual(await captured.clearChatConversation(), 'clear canceled');
  assert.strictEqual(chatLog().length, 1);
  assert.strictEqual(cleared.length, 0);
  assert.match(confirms[0].msg, /Clear this conversation/);
  assert.strictEqual(confirms[0].opts.danger, true);

  // Accepted: replay history AND the visible transcript clear together (emptying
  // the log is what deletes the §12 persisted copy — chatPersistence listens on it).
  allow = true;
  assert.strictEqual(await captured.clearChatConversation(), null);
  assert.strictEqual(cleared.length, 1);
  assert.strictEqual(chatLog().length, 0);
  forgetChatController(app);
  resetChatLog();
});

test('clearSharedConversation clears controller + log, and survives a missing controller', () => {
  resetChatLog();
  appendChatRow({ role: 'user', text: 'orphan row' });
  clearSharedConversation({});   // no controller ever built — still empties the log
  assert.strictEqual(chatLog().length, 0);
  resetChatLog();
});

test('a menu turn goes through the SAME controller, and history stays continuous', async () => {
  const app = {};
  const create = () => recordingController();
  // Panel turn first…
  const panelCtrl = sharedChatController(app, { create });
  await runChatTurn(panelCtrl, 'make it sepia');
  // …then a turn typed into the context menu (it looks the controller up the same way).
  const menuCtrl = sharedChatController(app, { create });
  const res = await runChatTurn(menuCtrl, 'rotate right', { signal: 'sig' });
  assert.strictEqual(menuCtrl, panelCtrl, 'the menu never builds a second controller');
  assert.deepStrictEqual(menuCtrl.calls.map((c) => c.text), ['make it sepia', 'rotate right']);
  assert.strictEqual(menuCtrl.calls[1].opts.signal, 'sig', 'the Stop AbortController signal is forwarded');
  assert.deepStrictEqual(menuCtrl.history.map((m) => m.text),
    ['make it sepia', 'ok', 'rotate right', 'ok'], 'one continuous conversation');
  assert.deepStrictEqual(res, { ok: true, text: 'did rotate right', entry: { reply: 'did rotate right', warnings: [], results: [] } });
  forgetChatController(app);
});

// ── One rendered transcript for both surfaces ──
test('the chat log is append/update/clear with subscribers — one row list, one order', () => {
  resetChatLog();
  const seen = [];
  const off = onChatLog((rows) => seen.push(rows.map((r) => `${r.role}:${r.text}`).join(' | ')));
  const user = appendChatRow({ role: 'user', text: 'make it sepia' });
  const pending = appendChatRow({ role: 'assistant', text: '…' });
  assert.notStrictEqual(user.id, pending.id, 'rows are keyed, so renderers can update in place');
  assert.deepStrictEqual(chatLog().map((r) => r.text), ['make it sepia', '…']);
  // The pending row becomes the reply — same id, so nothing re-renders from scratch.
  updateChatRow(pending.id, { text: 'Applied sepia.', results: [{ label: 'rotated', dataUrl: 'data:,' }] });
  assert.strictEqual(chatLog()[1].text, 'Applied sepia.');
  assert.strictEqual(chatLog()[1].results.length, 1);
  assert.strictEqual(chatLog().length, 2, 'updating never appends a duplicate');
  // Errors are flags on the row (both surfaces style them the same way).
  updateChatRow(pending.id, { text: 'Stopped.', error: true, results: undefined });
  assert.deepStrictEqual(
    { text: chatLog()[1].text, error: chatLog()[1].error },
    { text: 'Stopped.', error: true });
  // Every mutation notified every subscriber, in order.
  assert.deepStrictEqual(seen, [
    'user:make it sepia',
    'user:make it sepia | assistant:…',
    'user:make it sepia | assistant:Applied sepia.',
    'user:make it sepia | assistant:Stopped.',
  ]);
  clearChatLog();
  assert.deepStrictEqual(chatLog(), [], 'Clear empties it for everyone');
  assert.strictEqual(seen.at(-1), '');
  off();
  appendChatRow({ role: 'user', text: 'after unsubscribe' });
  assert.strictEqual(seen.length, 5, 'unsubscribed listeners stop hearing');
  resetChatLog();
});

test('a surface renders the history that already exists (not only live appends)', () => {
  resetChatLog();
  // A menu-only conversation…
  appendChatRow({ role: 'user', text: 'from the menu' });
  appendChatRow({ role: 'assistant', text: 'Done.' });
  // …and the panel wires up later: it paints from chatLog(), so it is never empty.
  const painted = [];
  onChatLog((rows) => painted.push(rows.length));
  const atWireTime = chatLog().map((r) => r.text);
  assert.deepStrictEqual(atWireTime, ['from the menu', 'Done.'], 'the log carries the backlog');
  const src = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  assert.ok(src.includes('paint();   // renders whatever the conversation already holds'),
    'the panel paints once at wire time, not only on change');
  const ctx = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  assert.ok(ctx.includes('onChatLog(paint);') && ctx.includes('      paint();'), 'and so does the flyout');
  resetChatLog();
  assert.strictEqual(painted.length, 0, 'no spurious notifications from reading');
});

test('both send loops run the SAME shared logged-turn frame', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  const menu = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  for (const [name, src] of [['panel', panel], ['flyout', menu]]) {
    assert.ok(src.includes('await runLoggedChatTurn('), `${name} runs the shared logged-turn frame`);
    assert.ok(src.includes('renderChatLog('), `${name} renders the log, never its own private DOM`);
  }
  // The frame itself (chatSession.js) owns the row writes: user turn, pending "…"
  // reply, and the in-place ok/error patch — so the surfaces cannot drift.
  const session = readFileSync(new URL('../js/llm/chatSession.js', import.meta.url), 'utf8');
  assert.ok(session.includes("appendChatRow({ role: 'user', text, attachments: attachmentPreviews(controller) });"),
    'the frame logs the user turn, carrying the images the user attached to it');
  assert.ok(session.includes("const pending = appendChatRow({ role: 'assistant', text: '…', pending: true });"),
    'the frame logs a pending reply, marked so the view can animate it');
  assert.ok(session.includes('updateChatRow(pending.id'), 'the frame resolves that row in place');
  // The panel's Clear runs the shared clear path (which repaints the flyout too).
  assert.ok(panel.includes('clearSharedConversation(app);'));
});

// An image dragged from another PAGE carries no File — only a URL in uri-list/html.
// The composer used to read Files alone, so such a drop silently attached nothing.
test('a dropped image URL is fetched into an attachment, not silently dropped', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  assert.ok(panel.includes('const files = mediaFilesFromData(e.dataTransfer);'), 'Files still win');
  assert.ok(panel.includes('const url = extractDraggedImageUrl((t) => e.dataTransfer.getData(t));'),
    'and a File-less drag falls back to its URL');
  assert.match(panel, /await attachFiles\(\[await fetchDraggedMediaFile\(url, \{ accept: \/\^\(image\|video\)\\\/\/ \}\)\]\);/);
  // A failure is reported, never swallowed — that silence was the whole bug.
  assert.ok(panel.includes("notify(`Couldn't attach that image — ${err.message}`, 'fail');"));
  assert.ok(panel.includes("notify('Nothing to attach from that drop', 'fail');"));
  // The fetch itself is the canvas's, shared rather than re-implemented.
  const drag = readFileSync(new URL('../js/core/dragImageUrl.js', import.meta.url), 'utf8');
  assert.match(drag, /export const fetchDraggedMediaFile = async \(url, \{ accept = \/\^image\\\/\/ \} = \{\}\)/);
  const binder = readFileSync(new URL('../js/core/controlsBinder.js', import.meta.url), 'utf8');
  assert.ok(binder.includes('const fetchUrlToFile = (url) => fetchDraggedMediaFile(url);'),
    'the canvas drop uses the same helper (no second copy)');
});

test('the composer ACTS on a drop; the panel SWALLOWS one (never the canvas)', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  // Attaching is the composer's.
  assert.ok(panel.includes("const dropRow = $('chat-input-wrap');"));
  for (const ev of ['dragover', 'dragleave', 'drop']) {
    assert.ok(panel.includes(`dropRow.addEventListener('${ev}'`), `${ev} is wired on the composer`);
  }
  // …but a drop that MISSES it must not fall through to the canvas: letting it
  // through popped the editor's "Open dropped image — replace or new page?" dialog,
  // which is never what dropping onto a chat means.
  assert.ok(panel.includes("host.addEventListener('drop'"), 'the panel takes the leftovers');
  assert.ok(panel.includes("notify('Drop it on the message box to attach it', 'info');"),
    'and says where to aim instead of silently eating it');
  // The whole panel owns drops, so the canvas never lights its zones under an open chat.
  assert.ok(panel.includes("host.toggleAttribute('data-drop-owner', on);"));
});

test('the attachment chip is a thumbnail, a name and a remove — nothing else', () => {
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.ok(view.includes("thumb.className = 'chat-attach-thumb';"), 'the queued picture is shown');
  assert.ok(view.includes('wireThumbPreview(thumb, label);'), 'and magnifies on hover');
  assert.ok(view.includes('name.dataset.title = label;'), 'the ellipsised name keeps the full one on the tooltip');
  assert.ok(view.includes('chip.append(name, rm);'), 'name + remove, no analyze/working pill');
  assert.ok(!view.includes('chat-attach-use'), 'the analyze ↔ working toggle is gone from the chip');
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.match(css, /\.chat-attach-thumb \{[^}]*object-fit: cover/);
  assert.match(css, /\.chat-attach-name \{[^}]*text-overflow: ellipsis/);
});

test('a fetched data: URL gets a readable filename, not its base64 payload', () => {
  // The chip showed "bXNxIKcJ5cNNW8QFr…" — the whole payload, read as a path segment.
  assert.strictEqual(fileNameForUrl('data:image/png;base64,iVBORw0KGgoAAA', 'image/png'), 'image.png');
  assert.strictEqual(fileNameForUrl('blob:http://x/9f2-ab', 'image/jpeg'), 'image.jpg');
  assert.strictEqual(fileNameForUrl('http://h/a/cat.png?v=2', 'image/png'), 'cat.png');
  assert.strictEqual(fileNameForUrl('http://h/photos/', 'image/webp'), 'image.webp');
  assert.strictEqual(fileNameForUrl('http://h/x', ''), 'x');   // a real segment is a real name
  // …but an opaque id longer than a filename is not one.
  assert.strictEqual(fileNameForUrl(`http://h/${'a'.repeat(120)}`, 'image/png'), 'image.png');
  // An extensionless segment with a KNOWN MIME is an endpoint, not a filename —
  // gstatic's /images?q=… named every chip "images".
  assert.strictEqual(
    fileNameForUrl('https://encrypted-tbn0.gstatic.com/images?q=tbn:ANd9GcT&s=10', 'image/jpeg'),
    'image.jpg');
});

test('hovering a small attachment thumbnail shows it large', () => {
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.ok(view.includes('export const wireThumbPreview = (img, caption = '), 'the preview helper exists');
  assert.ok(view.includes("wireThumbPreview(img, a.kind === 'video' ? `${a.name} (first frame)` : a.name);"),
    'and every transcript thumbnail is wired to it');
  assert.ok(view.includes("cap.textContent = caption;"), 'the filename is text, never markup');
  // On the BODY: the panel clips its overflow, so an in-place popup would be cut off.
  assert.ok(view.includes('document.body.appendChild(box);'));
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.match(css, /\.chat-thumb-preview \{[^}]*position: fixed;/);
  assert.match(css, /\.chat-thumb-preview \{[^}]*pointer-events: none;/, 'it must not steal its own hover');
  // A GLANCE, not a lightbox: the same clamp the projects modal's row zoom uses.
  assert.match(css, /\.chat-thumb-preview img \{[^}]*max-width: 25vw;[^}]*max-height: 20vh;/);
  const zoom = /\.project-thumb-zoom img \{([^}]*)\}/.exec(css);
  assert.ok(zoom && /max-width: 25vw/.test(zoom[1]) && /max-height: 20vh/.test(zoom[1]),
    'the two hover previews stay clamped by the same rule');
});

test('the scatter mesh is budgeted by how many rows leave at once', () => {
  const one = scatterGridFor(1);
  // A single removal keeps the finest grain the budget allows…
  assert.ok(one.cols * one.rows <= SCATTER_TILE_BUDGET);
  assert.ok(one.cols >= 24 && one.rows >= 12, 'a lone row still comes apart as dust');
  // …and a whole-transcript wipe coarsens until the TOTAL fits (2,028 blurred clones
  // at once is what made the extension's popup crawl).
  for (const n of [2, 6, 12, 20, 200]) {
    const g = scatterGridFor(n);
    const flying = Math.min(n, SCATTER_MAX_ROWS) * g.cols * g.rows;
    assert.ok(flying <= SCATTER_TILE_BUDGET * 1.05, `${n} rows → ${flying} tiles is over budget`);
    assert.ok(g.cols >= 8 && g.rows >= 4, 'never so coarse it reads as broken glass');
    assert.ok(g.cols <= 32 && g.rows <= 16);
  }
  // Past the cap the extra rows fade with no dust at all — a dozen simultaneous
  // scatters is already more than the eye resolves.
  assert.deepStrictEqual(scatterGridFor(20, SCATTER_MAX_ROWS), { cols: 0, rows: 0 });
  assert.ok(scatterGridFor(20, SCATTER_MAX_ROWS - 1).cols > 0);
  // Monotonic: more rows never means a finer per-row mesh.
  assert.ok(scatterGridFor(8).cols <= scatterGridFor(4).cols);
});

// ── Clearing: the rows leave FIRST, the empty state comes back after ──
test('the empty state waits out the wipe instead of appearing under the falling rows', () => {
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  // The placeholder is never painted in the same tick as the removal…
  assert.match(view, /const restoreEmptyState = \(transcript, log, wiped\) => \{/);
  assert.ok(view.includes('setTimeout(paint, wipeDurationMs());'), 'it waits for the wipe to finish');
  // …and when it finally runs it re-checks the world: a turn started during the wipe
  // must not be papered over with chips.
  assert.ok(view.includes("if (log.length || transcript.querySelector('[data-row]')) return;"));
  // One waiter at a time — renderChatLog runs on every log change.
  assert.ok(view.includes('if (transcript._emptyWaiting) return;'));
  // The wipe's true length is the SCATTER, not the row collapse: leaveThenRemove
  // resolves on LEAVE_MS while the particles keep falling for DISINTEGRATE_MS.
  const motion = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  assert.match(motion, /export const wipeDurationMs = \(\) => \{[\s\S]*Math\.max\(LEAVE_MS, DISINTEGRATE_MS\)/);
  assert.match(motion, /if \(motionReduced\(\)\) return 0;/, 'reduced motion has nothing to wait for');
  // …and neither has a mode with no particles in it: the row's own collapse IS the wipe.
  assert.match(motion, /dustEnabled\(\) \? Math\.max\(LEAVE_MS, DISINTEGRATE_MS\) : LEAVE_MS/);
});

// ── The images a turn carries belong to the USER's row ──
test('attachmentPreviews reduces the queued attachments to renderable thumbnails', () => {
  assert.deepStrictEqual(attachmentPreviews(null), []);
  assert.deepStrictEqual(attachmentPreviews({ attachments: [] }), []);
  const previews = attachmentPreviews({
    attachments: [
      { name: 'cat.jpg', kind: 'image', use: 'analyze', dataUrl: 'data:image/jpeg;base64,AAA' },
      // A video is represented by the frames actually sent to the model (§7).
      { name: 'clip.mp4', kind: 'video', frames: ['data:image/jpeg;base64,BBB', 'data:image/jpeg;base64,CCC'] },
      // Nothing renderable (a queue entry still decoding) is skipped, not shown blank.
      { name: 'pending.png', kind: 'image' },
    ],
  });
  assert.deepStrictEqual(previews, [
    { name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' },
    { name: 'clip.mp4', kind: 'video', dataUrl: 'data:image/jpeg;base64,BBB' },
  ]);
});

test('the logged turn hangs the attachments on the user row, and §12.1 still persists text only', async () => {
  resetChatLog();
  const controller = {
    attachments: [{ name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' }],
    async send() { return { reply: 'A cat.', warnings: [], results: [] }; },
  };
  await runLoggedChatTurn(controller, 'what is this?');
  const [user, assistantRow] = chatLog();
  assert.strictEqual(user.role, 'user');
  assert.deepStrictEqual(user.attachments, [{ name: 'cat.jpg', kind: 'image', dataUrl: 'data:image/jpeg;base64,AAA' }]);
  assert.strictEqual(assistantRow.attachments, undefined, 'the reply is not the one that attached them');
  // The transcript may show images; the persisted document must never hold them.
  const messages = rowsToMessages(chatLog());
  assert.deepStrictEqual(messages, [{ role: 'user', text: 'what is this?' }, { role: 'assistant', text: 'A cat.' }]);
  assert.ok(buildChatDoc(messages).messages.every((m) => !('attachments' in m) && !('images' in m)));
  resetChatLog();
});

test('renderChatLog paints the attachments as the user\'s own strip, above their message', () => {
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  // Keyed like the result cards, so a repaint updates rows in place instead of
  // reloading every thumbnail…
  assert.ok(view.includes('const attachId = `${row.id}-attachments`;'));
  assert.ok(view.includes('if (!transcript.querySelector(`[data-row="${attachId}"]`)) {'));
  // …and inserted BEFORE the message row (the images come with what was said).
  assert.ok(view.includes('el.before(strip);'), 'the strip precedes the message it belongs to');
  // The strip sits on the user's side — assistant-side would read as the model's.
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.match(css, /\.chat-attached \{[^}]*align-self: flex-end/);
  assert.match(css, /\.chat-attached-thumb \{[^}]*object-fit: cover/);
});

test('the drop target is the COMPOSER, cued by an animated icon over it', () => {
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  const cue = /\.chat-drop-cue \{([^}]*)\}/.exec(css);
  assert.ok(cue, 'a drag over the composer paints a labelled overlay');
  assert.match(cue[1], /position: absolute;/);
  assert.match(cue[1], /pointer-events: none;/, 'the overlay must not swallow the drop');
  // Shown only while the COMPOSER row is the drag target — not the whole panel.
  assert.match(css, /\.chat-input-wrap\.chat-drop-target \.chat-drop-cue \{ display: flex; \}/);
  assert.ok(!/stencil-chat-panel\.chat-drop-target::after/.test(css), 'the whole-panel overlay is gone');
  // Icon beside the label, and it animates (motion lives in animations.css).
  assert.ok(chatDropCueHtml().includes('chat-drop-cue-icon'));
  assert.match(chatDropCueHtml(), /Drop to attach/);
  const anims = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.match(anims, /\.chat-drop-cue-icon \{ animation: chat-drop-bob/);
  assert.match(anims, /@keyframes chat-drop-bob/);
  assert.match(anims, /prefers-reduced-motion: reduce\) \{\n    \.chat-drop-cue-icon \{ animation: none/);
  // And the drop overlay itself must clear the chat panel: at 9999 it painted BEHIND
  // the panel (z-index 90000), so dragging an image in with the chat open showed the
  // transcript where the drop zones belong.
  assert.match(css, /#global-drop-overlay \{[\s\S]*?z-index: 90002;/);
  assert.match(css, /chat-dock-left\) #global-drop-overlay \{ left: var\(--chat-size, 340px\); \}/);
});

// ── Pure helpers shared by both chat surfaces ──
test('replyWithWarnings appends unknown-op skips to the visible reply', () => {
  assert.strictEqual(replyWithWarnings({ reply: 'Done.', warnings: [] }), 'Done.');
  assert.strictEqual(replyWithWarnings({ reply: 'Done.', warnings: ['skipped op "zoom"'] }), 'Done.\n(skipped op "zoom")');
  assert.strictEqual(replyWithWarnings({ reply: 'Done.', warnings: ['a', 'b'] }), 'Done.\n(a; b)');
  assert.strictEqual(replyWithWarnings({ reply: 'Hi' }), 'Hi');
  assert.strictEqual(replyWithWarnings(null), '');
});

// Desktop parity: success notes ("opened cat.jpg in the editor first…") ride the
// reply as neutral text — ONE assistant bubble, never the error style.
test('a successful turn with warnings stays one NON-error assistant row', async () => {
  resetChatLog();
  const controller = {
    attachments: [],
    async send() {
      return { reply: 'Done.', warnings: ['opened cat.jpg in the editor first — the actions ran on it'], results: [] };
    },
  };
  await runLoggedChatTurn(controller, 'make it sepia');
  const rows = chatLog();
  assert.strictEqual(rows.length, 2, 'one user row + one assistant row, nothing extra');
  const assistantRow = rows[1];
  assert.strictEqual(assistantRow.role, 'assistant');
  assert.ok(assistantRow.text.includes('opened cat.jpg in the editor first'), 'the note rides the reply');
  assert.ok(!assistantRow.error, 'a warning is not an error — the row must stay neutral');
  resetChatLog();
  // And the view applies the red style only off that flag — never off warnings.
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  assert.ok(view.includes("(row.error ? ' chat-msg-error' : '')"), 'chat-msg-error is gated on row.error alone');
});

test('unreachableText names the provider and its endpoint (host only)', () => {
  assert.strictEqual(
    unreachableText({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, new Error('fetch failed')),
    "Couldn't reach Ollama at localhost:11434 (fetch failed)");
  assert.strictEqual(
    unreachableText({ provider: 'stencil-server', serverUrl: 'https://srv:8090', baseUrl: 'ignored' }, new Error('401')),
    "Couldn't reach Stencil server at srv:8090 (401)");
  assert.strictEqual(
    unreachableText({ provider: 'none' }, new Error('x')),
    'The assistant is turned off — choose a provider to enable it.');
  assert.ok(unreachableText({ provider: 'openai-compat', baseUrl: '' }, new Error('boom')).startsWith("Couldn't reach"));
});

test('describeChatError maps a failed turn to what both surfaces render', () => {
  const s = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
  const abort = new Error('aborted');
  abort.name = 'AbortError';
  assert.deepStrictEqual(describeChatError(abort, s), { kind: 'abort', text: 'Stopped.' });
  assert.deepStrictEqual(describeChatError(new LlmError('policy', 'refusal'), s), { kind: 'refusal', text: 'Refused: policy' });
  assert.deepStrictEqual(describeChatError(new LlmError('reply was cut off', 'truncated'), s),
    { kind: 'notice', text: 'reply was cut off' });
  assert.deepStrictEqual(describeChatError(new LlmError('no key', 'disabled'), s), { kind: 'notice', text: 'no key' });
  assert.strictEqual(describeChatError(new LlmError('500', 'http'), s).kind, 'unreachable');
  assert.strictEqual(describeChatError(new LlmError('bad config', 'config'), s).kind, 'unreachable');
  // fetch failures arrive pre-tagged by the client ('network'); a BARE TypeError
  // is not assumed to be one — canvas APIs in plan execution throw those too.
  assert.strictEqual(describeChatError(new LlmError('Failed to fetch', 'network'), s).kind, 'unreachable');
  assert.strictEqual(describeChatError(new TypeError("Failed to execute 'drawImage'"), s).kind, 'error');
  // A plain Error (e.g. an invalid plan) is textual, prefixed.
  assert.deepStrictEqual(describeChatError(new Error('action 2 invalid'), s),
    { kind: 'error', text: 'Error: action 2 invalid' });
});

test('runChatTurn surfaces a rejected turn instead of throwing', async () => {
  const boom = new LlmError('down', 'http');
  const ctrl = { send: async () => { throw boom; } };
  const res = await runChatTurn(ctrl, 'hi', { settings: { provider: 'ollama', baseUrl: 'http://localhost:11434' } });
  assert.strictEqual(res.ok, false);
  assert.strictEqual(res.kind, 'unreachable');
  assert.strictEqual(res.error, boom, 'the original error rides along (the panel rethrows it)');
  assert.ok(res.text.includes('localhost:11434'));
});

// ── Styling: the section must theme with the menu and fit its width ──
test('components.css sizes the assistant section for the menu in both themes', () => {
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  const block = css.slice(css.indexOf('.ctx-assist {'), css.indexOf('/* Hotkey hint shown'));
  assert.ok(block.includes('user-select: text'), 'chat text is selectable inside the user-select:none menu');
  assert.ok(/max-width: calc\(100vw - 40px\)/.test(block), 'never wider than the viewport');
  // The flyout is a tall chat WINDOW and the transcript flexes to fill it — a bare
  // min-height left the transcript sitting at its floor in a short box.
  assert.ok(block.includes('height: min(72vh, 600px)'), 'the chat window itself is tall, scaled to the viewport');
  assert.ok(/\.ctx-assist \{[^}]*display: flex;[^}]*flex-direction: column;/s.test(block), 'column layout drives the fill');
  assert.ok(block.includes('flex: 1 1 0'), 'the transcript takes every pixel the composer leaves');
  assert.ok(block.includes('min-height: 300px'), 'never shorter than 300px');
  assert.ok(block.includes('max-height: min(60vh, 520px)') && block.includes('overflow-y: auto'),
    'never taller than ~60vh, then scrolls in place — the flyout stays on-screen');
  assert.ok(block.includes('overscroll-behavior: contain'), 'transcript scrolling never chains to the page');
  // Theme vars only — no hardcoded colours (light/dark both follow the menu).
  assert.ok(/var\(--input-bg\)/.test(block) && /var\(--accent\)/.test(block) && /var\(--border-main\)/.test(block));
  assert.ok(!/#[0-9a-f]{6}/i.test(block), 'no hardcoded hex colours');
});

test('the flyout composer is resizable with the panel\'s slider handle', () => {
  const html = assistantItemHtml();
  // The strip sits between the attachments row and the composer, as in the panel.
  const iAttach = html.indexOf('id="ctx-assist-attachments"');
  const iSizer = html.indexOf('id="ctx-assist-sizer"');
  const iRow = html.indexOf('id="ctx-assist-input"');
  assert.ok(iAttach < iSizer && iSizer < iRow, 'sizer strip above the input, below the chips');
  // No tooltip: a grab handle explains itself, and the panel's carries none either.
  assert.ok(!/id="ctx-assist-sizer"[^>]*title=/.test(html), 'the sizer needs no tooltip');
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // Wired with the SAME helper as the panel; a drag re-places the FLYOUT only…
  assert.ok(src.includes("wireInputSizer(document.getElementById('ctx-assist-sizer'), input, {"));
  assert.ok(src.includes('onDrag: () => { if (flyout.classList.contains(\'ctx-sub-visible\')) positionSub(item, flyout); },'),
    'the flyout re-places itself as the composer grows');
  assert.strictEqual(src.split('placeMenu').length - 1, 2, 'and the ROOT menu is still placed once per open');
  // …and the drag marks the surface engaged, so a moving flyout is never "left".
  assert.ok(src.includes('hold: (on) => { resizing = on; },'));
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  assert.ok(panel.includes('wireInputSizer(inputSizer, input, { host });'), 'the panel uses the same helper');
  // Clamped by CSS (session-only: the drag writes an inline height, nothing persists).
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  const inputRule = css.slice(css.indexOf('#ctx-assist-input {'), css.indexOf('}', css.indexOf('#ctx-assist-input {')));
  assert.ok(inputRule.includes('resize: none') && inputRule.includes('min-height: 44px'), 'no native grip; two-row floor');
  // A px cap, deliberately: a percentage max-height resolves against the content-sized
  // composer row and silently pins the input to ~60% of the dragged height.
  assert.ok(inputRule.includes('max-height: 200px') && !inputRule.includes('max-height: 60%'), 'absolute cap');
  assert.ok(css.includes('.chat-input-sizer::before, .ctx-assist-sizer::before {'), 'shares the pill affordance');
  assert.ok(css.includes('.ctx-assist-sizer:hover::before, .ctx-assist-sizer.dragging::before'), 'and its hover/drag accent');
});

// ── Menu-open guards (the behaviour that makes chatting in a menu possible) ──
test('contextMenu.js keeps the menu open while chatting', () => {
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // Scroll-close ignores scrolls that originate INSIDE the menu (the transcript).
  assert.match(src, /if \(e\.target && e\.target\.nodeType && menu\.contains\(e\.target\)\) return;/);
  // Inside clicks never reach the document mousedown close handler.
  assert.ok(src.includes("menu.addEventListener('mousedown', e => e.stopPropagation())"));
  // A plan re-laying out the canvas scrolls the viewport — that scroll is the
  // assistant's, not the user's, so it must not dismiss the menu either.
  assert.ok(src.includes('if (assistantBusy()) return;'), 'assistant-caused scrolls are exempt');
  assert.match(src, /const assistantBusy = \(\) => assistSending \|\| Date\.now\(\) < assistBusyUntil;/);
  // The entry is (re)built and re-moded on open and when the provider changes.
  assert.ok(src.includes("window.addEventListener('stencil:llm-settings-changed', syncAssistant)"));
  assert.ok(src.includes('syncAssistant();\n      menu.style.left'), 'entry settled before the menu is measured');
  // Typing in the flyout (or a running turn) suppresses the hover-out close, but
  // hovering a SIBLING parent still closes it like any other flyout.
  assert.ok(src.includes("flyout._keepOpen = () => assistSending || resizing || chatRowMenuOpen() || flyout.contains(document.activeElement);"),
    'typing, resizing the composer, an open row menu, or a running turn all count as engaged');
  assert.match(src, /const keepSubOpen = \(sub\) => !!sub\._keepOpen\?\.\(\);/);
  assert.ok(src.includes('if (keepSubOpen(sub)) return;'), 'hideSub honours the engaged flyout');
  // The ONLY closeMenu() uses in the assistant wiring are the four that open something
  // else on top: the settings gear, the Configure-provider CTA in an error card, the
  // Reconnect CTA on an expired-session card, and the phone hand-over to the panel.
  // Chatting itself never closes it.
  const assist = src.slice(src.indexOf('const wireAssistant ='), src.indexOf('const openChatPanel ='));
  assert.strictEqual(assist.split('closeMenu').length - 1, 4,
    'gear + settings CTA + reconnect CTA + phone hand-over only');
});

test('unreachableText quotes an endpoint that ANSWERED instead of guessing it is down', () => {
  const err = new Error("model 'qwen2.5:0.5b' not found, try pulling it first");
  err.answered = true;
  assert.strictEqual(
    unreachableText({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, err),
    "Ollama at localhost:11434: model 'qwen2.5:0.5b' not found, try pulling it first");
});

// The endpoint is a LABEL on the reason, never a second sentence around it: the
// server says the reason once (contract §6.3) and the card must not restate it.
test('an answered endpoint adds the host and nothing else', () => {
  const err = new LlmError('the LLM provider is out of credits or has no active billing', 'http');
  err.answered = true;
  err.status = 502;
  assert.strictEqual(
    unreachableText({ provider: 'stencil-server', serverUrl: 'http://localhost:8090' }, err),
    'Stencil server at localhost:8090: the LLM provider is out of credits or has no active billing');
  const shown = describeChatError(err, { provider: 'stencil-server', serverUrl: 'http://localhost:8090' });
  assert.strictEqual(shown.kind, 'unreachable');   // keeps the "Configure provider" CTA
  assert.ok(!/answered|HTTP 502|credit balance/.test(shown.text), shown.text);
});

// A stopped turn is a change of mind, not a dead end: the row keeps the prompt so the
// renderer can offer Retry (browser, extension and desktop all do this now).
test('stopping a turn leaves a Retry with the original text', async () => {
  resetChatLog();
  const controller = { send: async () => { const e = new Error('aborted'); e.name = 'AbortError'; throw e; } };
  await runLoggedChatTurn(controller, 'outline the cat', { settings: { provider: 'ollama' } });
  const rows = chatLog();
  const reply = rows[rows.length - 1];
  assert.strictEqual(reply.text, 'Stopped.');
  assert.strictEqual(reply.retryText, 'outline the cat', 'the prompt survives the stop');
  assert.strictEqual(reply.pending, false, 'the pending mark is cleared, so the dots stop');
});

test('every bare identifier contextMenu.js uses from other llm modules is imported', () => {
  const src = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  // Regression: attachFull referenced MAX_ATTACHMENTS without importing it (runtime-only crash).
  assert.ok(src.includes("import { MAX_ATTACHMENTS } from '../llm/chatController.js';"),
    'MAX_ATTACHMENTS is imported where the attach-cap check uses it');
});

// ── The closed-chat balloon, built ONCE for every surface ───────────────────
// The panel framed the outcome ("Assistant finished (2 images) — …") and offered a way
// back; the context-menu flyout toasted a bare truncated reply with no framing and no
// click action. Both now build the same thing.
test('closedTurnToast: one framing for both surfaces, and silence for an abort', async () => {
  const { closedTurnToast, CHAT_TOAST_CHARS, EMPTY_REPLY_TEXT } = await import('../js/llm/chatSession.js');
  assert.deepStrictEqual(closedTurnToast({ ok: true, entry: { reply: 'Cropped it.', results: [] } }),
    { text: 'Assistant finished — Cropped it.', type: 'ok' });
  // The image count rides the framing, pluralised.
  assert.strictEqual(closedTurnToast({ ok: true, entry: { reply: 'Two ways.', results: [1, 2] } }).text,
    'Assistant finished (2 images) — Two ways.');
  assert.strictEqual(closedTurnToast({ ok: true, entry: { reply: 'One.', results: [1] } }).text,
    'Assistant finished (1 image) — One.');
  // A wordless turn still says something (the transcript's own empty-answer line).
  assert.ok(closedTurnToast({ ok: true, entry: { reply: '', results: [] } }).text.includes(EMPTY_REPLY_TEXT.slice(0, 20)));
  // Failures name the cause; aborts are the user's own doing and say nothing at all.
  assert.deepStrictEqual(closedTurnToast({ ok: false, kind: 'error', error: { message: 'boom' } }),
    { text: 'Assistant failed — boom', type: 'fail' });
  assert.strictEqual(closedTurnToast({ ok: false, kind: 'abort', text: 'Stopped.' }), null);
  assert.strictEqual(closedTurnToast(null), null);
  // …and it is truncated for the balloon, wherever it came from.
  const long = closedTurnToast({ ok: true, entry: { reply: 'x'.repeat(400), results: [] } });
  assert.strictEqual(long.text.length, CHAT_TOAST_CHARS);
  assert.ok(long.text.endsWith('…'));
});

test('both surfaces toast through the shared builder, each with a way back to the chat', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  const menu = readFileSync(new URL('../js/ui/contextMenu.js', import.meta.url), 'utf8');
  for (const [name, src] of [['panel', panel], ['flyout', menu]]) {
    assert.ok(src.includes('closedTurnToast('), `${name} builds its toast from the shared helper`);
    assert.ok(!/truncateForToast\(/.test(src), `${name} no longer frames its own`);
    assert.ok(/notify\(toast\.text, toast\.type, \{ onClick:/.test(src), `${name}'s toast reopens the chat`);
  }
  // The flyout cannot restore itself (it needs the menu at its old point), so it opens
  // the docked panel — the same conversation.
  assert.ok(menu.includes('onClick: () => app.chat?.open()'));
  assert.ok(panel.includes('onClick: () => setOpen(true)'));
});

// ── Unread affordance on the toolbar button ────────────────────────────────
test('a turn landing on a closed chat toasts, and only WORK IN FLIGHT marks the button', () => {
  const panel = readFileSync(new URL('../js/ui/chatPanel.js', import.meta.url), 'utf8');
  // The toast fires only while no surface can show the answer…
  const closed = panel.slice(panel.indexOf('const closedToast = (res) => {'), panel.indexOf('// ── Send loop'));
  assert.ok(closed.includes('if (panelIsOpen() || !toast) return;'), 'never while the chat is visible');
  assert.ok(closed.includes('onClick: () => setOpen(true)'), 'and the toast itself opens the chat');
  // …and it leaves NOTHING behind on the icon: no unread badge on either surface
  // (the desktop's twin went with it — mainWindowChat.cpp).
  assert.ok(!panel.includes('chat-unread') && !panel.includes('markChatUnread'),
    'no unread dot is marked anywhere');
  // In-flight behind a closed chat still gets the quiet pulse, cleared in cleanup.
  assert.ok(panel.includes('markChatBusy(!panelIsOpen());'));
  assert.ok(panel.includes('markChatBusy(false);'));
  // A pure pseudo-element dot: the button's box never moves.
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  assert.ok(!css.includes('chat-unread'), 'and no unread rule is left in the stylesheet');
  const dot = css.slice(css.indexOf('#chat-btn.chat-working::before'), css.indexOf('/* On the accent-filled'));
  assert.match(dot, /position: absolute/);
  assert.match(dot, /background: var\(--accent\)/, 'accent-coloured');
  assert.match(css, /#chat-btn\.active\.chat-working::before/, 'and it inverts on the accent fill');
});

// ── The chat provider hits the same dead session ───────────────────────────
// stencil-server posts to /llm/chat with the same bearer the projects list uses, so a
// stale token 401s there too. It is not "the provider is unreachable" — the server is
// answering; the session is over, and the cure is the same reconnect.
test('an expired stencil-server session is named as such in the chat card', async () => {
  const { describeChatError } = await import('../js/llm/chatSession.js');
  const { LlmError } = await import('../js/llm/llmClient.js');
  const http = (msg, status) => Object.assign(new LlmError(msg, 'http'), { status, answered: true });
  const settings = { provider: 'stencil-server', serverUrl: 'http://localhost:8090' };
  const out = describeChatError(http('unauthorized', 401), settings);
  assert.strictEqual(out.kind, 'expired', 'its own kind — not "unreachable"');
  assert.match(out.text, /session on localhost:8090 has expired/);
  assert.match(out.text, /reconnect to that server/);
  assert.strictEqual(out.serverUrl, 'http://localhost:8090', 'the card knows WHICH server');
  // 403 counts too…
  assert.strictEqual(describeChatError(http('forbidden', 403), settings).kind, 'expired');
  // …but the same status from a LOCAL provider is an ordinary unreachable card: there
  // is no Stencil session to renew there.
  const ollama = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
  assert.strictEqual(describeChatError(http('nope', 401), ollama).kind, 'unreachable');
  // …and a plain server error stays unreachable.
  assert.strictEqual(describeChatError(http('boom', 500), settings).kind, 'unreachable');
});

test('the expired card carries a Reconnect CTA instead of Configure provider', async () => {
  const view = readFileSync(new URL('../js/ui/chatView.js', import.meta.url), 'utf8');
  // The row patch marks it, the renderer picks the CTA off that mark…
  const session = readFileSync(new URL('../js/llm/chatSession.js', import.meta.url), 'utf8');
  assert.match(session, /card: res\.kind === 'unreachable' \|\| res\.kind === 'expired'/);
  assert.match(session, /reconnect: res\.kind === 'expired' \? \(res\.serverUrl \|\| ''\) : null/);
  assert.ok(view.includes("const cta = row.reconnect ? '.chat-reconnect-cta' : '.chat-config-cta';"));
  assert.ok(view.includes('chatReconnectButton(row.reconnect, onReconnect)'));
  // …exactly one of the two lives on a row, whichever it is.
  assert.ok(view.includes("if (!row.card || row.reconnect) el.querySelector('.chat-config-cta')?.remove();"));
  assert.ok(view.includes("if (!row.card || !row.reconnect) el.querySelector('.chat-reconnect-cta')?.remove();"));
  // Both surfaces route it to the Connections modal.
  for (const f of ['../js/ui/chatPanel.js', '../js/ui/contextMenu.js']) {
    const src = readFileSync(new URL(f, import.meta.url), 'utf8');
    assert.ok(/onReconnect: \(\) =>/.test(src), `${f} wires the hook`);
    assert.ok(src.includes("document.getElementById('connect-btn')?.click()"), `${f} opens Connections`);
  }
});

test('chatReconnectButton names the server it will sign in to', async () => {
  globalThis.document = {
    createElement: () => ({ className: '', innerHTML: '', _l: {},
      addEventListener(t, fn) { this._l[t] = fn; }, click() { this._l.click?.(); } }),
  };
  const { chatReconnectButton } = await import('../js/ui/chatView.js?reconnect-cta');
  let asked = null;
  const b = chatReconnectButton('http://localhost:8090', (u) => { asked = u; });
  assert.ok(b.className.includes('chat-reconnect-cta'));
  assert.match(b.innerHTML, /Reconnect to localhost:8090/, 'the scheme is dropped, the host is not');
  b.click();
  assert.strictEqual(asked, 'http://localhost:8090', 'and it hands the URL back');
  // No URL known → a plain label, never "Reconnect to undefined".
  assert.match(chatReconnectButton('', () => {}).innerHTML, /<span>Reconnect<\/span>/);
});
