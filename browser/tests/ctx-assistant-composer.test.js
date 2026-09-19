// The flyout composer (js/ui/contextMenu.js + js/llm/chatSession.js): the touch rule that
// hands over to the panel, the action row, queued attachments and the cached provider probe.
import { test } from 'node:test';
import assert from 'node:assert';
import { assistantItemHtml } from '../js/ui/contextMenu.js';
import { isTouchLike, TOUCH_MEDIA } from '../js/utils.js';
import {
  sharedChatController, queueAttachments, cacheProbe, cachedProbe, forgetProbe, probeStatusClass,
  PROBE_TTL_MS,
} from '../js/llm/chatSession.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from './helpers/css.js';
import { chatViewSource } from './helpers/chatViewSource.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';

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
  const src = contextMenuSource();
  assert.ok(src.includes('const plain = isTouchLike();'));
  assert.ok(!src.includes('(max-width: 680px)'), 'no private copy of the breakpoint');
});

test('plain mode is applied per open and hands over to the chat panel', () => {
  const src = contextMenuSource();
  // Decided at open time (syncAssistant runs from openAt) and on resize while open.
  assert.ok(src.includes('const plain = isTouchLike();'));
  assert.ok(src.includes("item.dataset.noSub = plain ? '1' : '0';"));
  assert.ok(src.includes("item.classList.toggle('ctx-assist-plain', plain);"));
  assert.ok(src.includes("onWindowResize(() => { if (host.menuIsOpen()) syncAssistant(); });"));
  // Hover can't open a flyout in plain mode; a click closes the menu and opens the panel.
  assert.ok(src.includes("if (item.dataset.noSub === '1') { closeActiveSub(); return; }"));
  assert.match(src, /if \(item\.dataset\.noSub === '1'\) \{[\s\S]{0,160}closeMenu\(\);[\s\S]{0,80}openChatPanel\(\);/);
  // The panel is opened through the shared facade (same conversation), with a fallback.
  assert.ok(src.includes("if (typeof app?.chat?.open === 'function') app.chat.open();"));
  assert.ok(src.includes("document.getElementById('chat-btn')?.click();"));
  // CSS drops the caret and hard-blocks the flyout in plain mode.
  const css = COMPONENTS_CSS;
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
  const css = COMPONENTS_CSS;
  assert.ok(css.includes('.chat-abtn, .ctx-assist-abtn { width: 34px; height: 34px;'), 'the panel\'s 34px treatment, shared');
  // The flyout adds no disabled treatment of its own: send inherits the app-wide
  // `button:disabled` styling exactly as the panel's send does, so the two cannot drift.
  assert.strictEqual(css.split('.ctx-assist-abtn:disabled').length - 1, 0, 'no bespoke disabled rule');
  assert.ok(LAYOUT_CSS.includes('button:disabled,'),
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
  const src = contextMenuSource();
  // The picker wiring is the SHARED composer helper (chatView.js), fed by the shared
  // queueing helper on the SHARED controller, then both rows repaint.
  const view = chatViewSource();
  assert.ok(view.includes("attachBtn.addEventListener('click', () => attachInput.click());"), 'picker wiring lives in the shared composer');
  assert.ok(src.includes('wireChatComposer({ input, sendBtn, attachBtn, attachInput }'), 'the flyout wires through it');
  assert.match(src, /await queueAttachments\(sharedChatController\(app\),\s*files,/);
  assert.ok(src.includes('notifyAttachmentsChanged();'), 'the panel row repaints too');
  assert.ok(src.includes('subscribe(CHAT_ATTACHMENTS_EVENT, renderAttachments);'), 'and this one listens back');
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
