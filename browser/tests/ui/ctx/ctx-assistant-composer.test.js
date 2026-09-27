// The flyout composer (js/ui/contextMenu/contextMenu.js + js/llm/chat/session.js): the touch rule that
// hands over to the panel, the action row, queued attachments and the cached provider probe.
import { test } from 'node:test';
import assert from 'node:assert';
import { assistantItemHtml } from '../../../js/ui/contextMenu/contextMenu.js';
import { isTouchLike, TOUCH_MEDIA } from '../../../js/utils.js';
import {
  sharedChatController, queueAttachments, cacheProbe, cachedProbe, forgetProbe, probeStatusClass,
  PROBE_TTL_MS,
} from '../../../js/llm/chat/session.js';
import { loadLlmSettings } from '../../../js/llm/settings.js';
import { syncComposerControls, CHAT_ATTACHMENTS_EVENT } from '../../../js/ui/chat/view.js';
import { publish } from '../../../js/eventBus/appBus.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from '../../helpers/css.js';
import { mountContextMenu } from '../../helpers/ctxMenuMountRig.js';

const OLLAMA = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
const settle = () => new Promise((r) => setImmediate(r));

// ── Desktop = flyout chat · phone / touch = plain item that opens the panel ──
test('plain mode rides the app-wide touch rule (utils.js), not a private query', async (t) => {
  const mm = (matches) => (q) => ({ matches: q === TOUCH_MEDIA && matches });
  assert.strictEqual(isTouchLike(mm(false)), false, 'desktop → submenu flyout');
  assert.strictEqual(isTouchLike(mm(true)), true, 'phone width / coarse pointer → plain item');
  // The query is the panel's own phone breakpoint plus the no-hover case.
  assert.ok(TOUCH_MEDIA.includes('(max-width: 680px)'), 'same phone breakpoint as the chat modal');
  assert.ok(TOUCH_MEDIA.includes('(hover: none) and (pointer: coarse)'), 'no hover ⇒ no flyout');
  // No matchMedia at all (Node, old engines) → desktop behaviour, never a dead item.
  assert.strictEqual(isTouchLike(null), false);
  assert.strictEqual(isTouchLike(() => { throw new Error('bad query'); }), false);
  // The menu asks the shared helper: a page that answers ONLY the shared query flips it.
  const m = await mountContextMenu(t, { settings: OLLAMA });
  assert.strictEqual(m.$('ctx-assist-menu').dataset.noSub, '0', 'desktop → the flyout');
  m.env.touch = true;
  m.open();
  assert.strictEqual(m.$('ctx-assist-menu').dataset.noSub, '1', 'the shared touch rule → plain');
});

test('plain mode is applied per open and hands over to the chat panel', async (t) => {
  const opened = [];
  const m = await mountContextMenu(t, { settings: OLLAMA, app: { chat: { open: () => opened.push(m.isOpen()) } } });
  const item = m.$('ctx-assist-menu');
  // Decided at open time (syncAssistant runs from openAt) and on resize while open.
  m.env.touch = true;
  m.open();
  assert.ok(item.dataset.noSub === '1' && item.classList.contains('ctx-assist-plain'));
  m.env.touch = false;
  m.resize();
  assert.ok(item.dataset.noSub === '0' && !item.classList.contains('ctx-assist-plain'), 're-evaluated on resize while open');
  m.env.touch = true;
  m.open();
  // Hover can't open a flyout in plain mode; a click closes the menu and opens the panel.
  m.pointer(70, 80);
  item.dispatch('mouseenter');
  assert.ok(!m.visible('ctx-assist-sub'), 'no flyout on hover');
  item.dispatch('click');
  assert.deepStrictEqual([m.isOpen(), opened], [false, [false]], 'the menu closes, then the panel opens');
  // The panel is opened through the shared facade (same conversation), with a fallback.
  const bare = await mountContextMenu(t, { settings: OLLAMA });
  bare.env.touch = true;
  bare.open();
  bare.$('ctx-assist-menu').dispatch('click');
  assert.strictEqual(bare.$('chat-btn').clicks, 1, 'no facade → the chat button');
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

test('attach queues into the shared controller; the gear opens the one settings modal', async (t) => {
  const app = {};
  const shared = { attachments: [], async addAttachment(f) { this.attachments.push({ name: f.name, kind: 'image', dataUrl: 'data:,' }); } };
  sharedChatController(app, { create: () => shared });
  const m = await mountContextMenu(t, { settings: OLLAMA, app });
  const { $ } = m;
  // The picker wiring is the SHARED composer helper, fed into the SHARED controller.
  $('ctx-assist-attach-btn').click();
  assert.strictEqual($('ctx-assist-attach-input').clicks, 1, 'the button opens the picker');
  const announced = [];
  window.addEventListener(CHAT_ATTACHMENTS_EVENT, () => announced.push(1));
  $('ctx-assist-attach-input').dispatch('change', { target: { files: [{ name: 'cat.png', type: 'image/png' }], value: 'x' } });
  await settle();
  const chips = () => $('ctx-assist-attachments').children.map((c) => c.textContent);
  assert.deepStrictEqual([shared.attachments.map((a) => a.name), chips()], [['cat.png'], ['cat.png']]);
  assert.strictEqual(announced.length, 1, 'the panel row repaints too');
  shared.attachments.push({ name: 'dog.png', kind: 'image', dataUrl: 'data:,' });
  publish(CHAT_ATTACHMENTS_EVENT);
  assert.deepStrictEqual(chips(), ['cat.png', 'dog.png'], 'and this one listens back');
  // Attach pauses mid-turn (and at the queue cap) — the shared control sync.
  const ctl = { sendBtn: m.doc.createElement('button'), attachBtn: m.doc.createElement('button'), input: { value: '' } };
  for (const [sending, attachFull, off] of [[true, false, true], [false, true, true], [false, false, false]]) {
    syncComposerControls(ctl, sending, { attachFull });
    assert.strictEqual(ctl.attachBtn.disabled, off, `sending ${sending}, full ${attachFull}`);
  }
  // Gear → its own rect is captured (this popup is about to hide it) and handed to the ONE
  // settings modal directly, so it flies from THIS gear, not the panel's.
  const opened = [];
  $('chat-settings-overlay').__stencilModal = { open: (rect) => opened.push(rect) };
  $('ctx-assist-settings-btn').getBoundingClientRect = () => (m.isOpen() ? { left: 7, top: 9 } : null);
  m.open();
  $('ctx-assist-settings-btn').click();
  assert.deepStrictEqual([m.isOpen(), opened], [false, [{ left: 7, top: 9 }]]);
});

test('the flyout status dot reads the shared probe first, and probes only when nothing is known', async (t) => {
  forgetProbe();
  const fetched = [];
  const m = await mountContextMenu(t, { settings: OLLAMA });
  const realFetch = globalThis.fetch;
  globalThis.fetch = async (url) => { fetched.push(url); return { ok: true, status: 200, json: async () => ({ models: [] }) }; };
  t.after(() => { globalThis.fetch = realFetch; forgetProbe(); });
  const dot = m.$('ctx-assist-status-dot');
  cacheProbe(loadLlmSettings(), { ok: true });
  m.$('ctx-assist-menu').dispatch('mouseenter');
  assert.deepStrictEqual([dot.className, fetched.length], ['conn-status conn-status-connected', 0], 'the panel\'s probe, for free');
  forgetProbe();
  m.$('ctx-assist-menu').dispatch('mouseenter');
  assert.strictEqual(dot.className, 'conn-status conn-status-connecting', 'amber while it asks');
  await settle();
  assert.strictEqual(fetched.length, 1, 'one probe when nothing fresh is known');
  assert.ok(cachedProbe(loadLlmSettings()), 'and its answer is cached for the other surface');
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
