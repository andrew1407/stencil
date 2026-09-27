// The context menu's "Assistant ▸" entry (js/ui/contextMenu/contextMenu.js): a submenu parent whose
// flyout is a compact chat, gated on the configured provider and built by syncAssistant.
import { test } from 'node:test';
import assert from 'node:assert';
import { assistantEnabled, assistantItemHtml } from '../../../js/ui/contextMenu/contextMenu.js';
import { menuPopOrigin } from '../../../js/ui/motion.js';
import { publish, EVENTS } from '../../../js/eventBus/appBus.js';
import { ANIMATIONS_CSS } from '../../helpers/css.js';
import { layoutWith } from '../../helpers/ctxAssistantRig.js';
import { mountContextMenu } from '../../helpers/ctxMenuMountRig.js';

const OLLAMA = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
const sibling = (el, d) => el.parentElement.children[el.parentElement.children.indexOf(el) + d];

const ASSIST_IDS = [
  'ctx-assist-menu', 'ctx-assist-sub', 'ctx-assist', 'ctx-assist-transcript',
  'ctx-assist-attachments', 'ctx-assist-sizer', 'ctx-assist-input', 'ctx-assist-send',
  'ctx-assist-attach-btn', 'ctx-assist-attach-input', 'ctx-assist-settings-btn', 'ctx-assist-status-dot',
];


// ── Presence / absence ──
test('the Assistant entry markup carries each id once, above Start Drawing', async (t) => {
  const html = assistantItemHtml();
  for (const id of ASSIST_IDS) {
    assert.strictEqual(html.split(`id="${id}"`).length - 1, 1, `id="${id}" appears exactly once`);
  }
  // Same placeholder wording as the panel, and Enter/Shift+Enter documented in it.
  assert.ok(html.includes('placeholder="Ask the assistant… (Enter sends, Shift+Enter newline)"'));
  // Built by syncAssistant directly ABOVE the script entry, itself above Start Drawing.
  const m = await mountContextMenu(t, { settings: OLLAMA });
  assert.strictEqual(sibling(m.$('ctx-assist-menu'), 1).id, 'ctx-script', 'inserted immediately above the script entry');
  const bare = await mountContextMenu(t, { settings: OLLAMA, before: (doc) => doc.getElementById('ctx-script').remove() });
  assert.strictEqual(sibling(bare.$('ctx-assist-menu'), 1).id, 'ctx-draw-toggle', 'falling back to Start Drawing');
  // Send ships disabled (nothing typed yet) and becomes Stop at runtime.
  assert.ok(html.includes('id="ctx-assist-send" disabled'));
});

// The chat must be a SUBMENU and the root menu must never be re-clamped while it is open:
// moving it under a stationary cursor fires mouseleave and kills the hovered item's flyout.
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

test('the root menu is never re-positioned/observed while open (submenu-killer guard)', async (t) => {
  const m = await mountContextMenu(t, { settings: OLLAMA });
  // Only the FLYOUTS may be observed: a ResizeObserver on the MENU re-clamps it as the chat
  // grows, sliding the hovered item out from under the cursor.
  const targets = m.observed.map((o) => o.target);
  assert.ok(!targets.includes(m.menu), 'no ResizeObserver on #ctx-menu');
  const flyouts = targets.filter((el) => el.classList.contains('ctx-sub'));
  assert.strictEqual(new Set(flyouts).size, flyouts.length, 'exactly one observer per flyout');
  // Every submenu parent (incl. the late-built Assistant) goes through the one wiring path.
  for (const item of m.menu.querySelectorAll(':scope > .ctx-item')) {
    const sub = item.querySelector(':scope > .ctx-sub');
    if (sub) assert.ok(flyouts.includes(sub), `${item.id}'s flyout is wired like the rest`);
  }
  assert.ok(flyouts.includes(m.$('ctx-assist-sub')), 'the built-later Assistant included');
  // placeMenu runs once per open, from openAt, with the click point.
  m.open(1000, 700);
  const placed = [m.menu.style.left, m.menu.style.top];
  assert.deepStrictEqual(placed, ['918px', '700px'], 'clamped from the click point');
  m.pointer(990, 690);
  m.$('ctx-assist-menu').dispatch('mouseenter');
  assert.ok(m.visible('ctx-assist-sub'), 'the Assistant opens its flyout on hover');
  m.observed.filter((o) => flyouts.includes(o.target)).forEach((o) => o.fire());
  assert.deepStrictEqual([m.menu.style.left, m.menu.style.top], placed, 'a growing flyout never moves the root');
});

test('flyouts survive the menu moving under a stationary cursor', async (t) => {
  const m = await mountContextMenu(t, { settings: OLLAMA });
  // A flyout placed while the entry pop still holds a transform anchors to the MENU, so the pop
  // is finished first — before the flyout is parked off-screen for its measurement.
  const atFinish = [];
  m.menu.getAnimations = () => [{ finish: () => atFinish.push(m.$('ctx-layout-sub').style.left) }];
  m.open();
  const [layout, assist] = [m.$('ctx-layout-menu'), m.$('ctx-assist-menu')];
  m.pointer(55, 65);
  layout.dispatch('mouseenter');
  assert.ok(atFinish.length && !atFinish.includes('-9999px'), 'entry pop is settled before placing a flyout');
  const anims = ANIMATIONS_CSS;
  assert.ok(/#ctx-menu\.ctx-open \{[^}]*animation: menuPop[^}]*\}/.test(anims), 'the pop is still there…');
  assert.ok(!/#ctx-menu\.ctx-open \{[^}]*animation: menuPop[^;]*both/.test(anims), '…and still not `both`-filled');
  // The pop moves items under a still cursor, which fires SYNTHETIC boundary events: with no
  // pointer motion since the placement there is no stealing and no closing.
  assist.dispatch('mouseenter');
  assert.ok(m.visible('ctx-layout-sub') && !m.visible('ctx-assist-sub'), 'no flyout stealing without motion');
  m.$('ctx-fit-window').dispatch('mouseenter');
  assert.ok(m.visible('ctx-layout-sub'), 'no closing without motion');
  for (const type of ['mousemove', 'mouseover', 'mouseout']) {
    assert.ok(m.doc.listeners(type).some((l) => l.capture), `${type} is sampled — it precedes mouseenter/mouseleave`);
  }
  m.pointer(60, 70);
  assist.dispatch('mouseenter');
  assert.ok(m.visible('ctx-assist-sub') && !m.visible('ctx-layout-sub'), 'a real move hands the hover over');
  // The item moved, not the user — the open flyout follows it.
  m.$('ctx-assist-sub').classList.remove('ctx-sub-fresh');
  m.observed.find((o) => o.target === m.$('ctx-assist-sub')).fire();
  assert.ok(m.$('ctx-assist-sub').classList.contains('ctx-sub-fresh'), 'a content resize re-places the open flyout');
});

test('the menu pops out of the click point, and Escape (consumed) closes it', async (t) => {
  const m = await mountContextMenu(t, { settings: OLLAMA });
  // placeMenu stamps transform-origin from the click point vs the PLACED (clamped) box.
  m.open(1000, 700);
  assert.strictEqual(m.menu.style.transformOrigin, menuPopOrigin(1000, 700, { left: 918, top: 700, width: 100, height: 20 }),
    'the origin is the click point relative to the clamped position');
  const anims = ANIMATIONS_CSS;
  assert.match(anims, /@keyframes menuPop \{ from \{ opacity: 0; transform: scale\(0\.62\); \} to \{ opacity: 1; transform: none; \} \}/,
    'the pop scales up from the origin');
  assert.match(anims, /prefers-reduced-motion: reduce\) \{\s*#ctx-menu\.ctx-open, \.chat-row-menu \{ animation: none; \}/,
    'both menus opt out under prefers-reduced-motion');
  // Escape: capture phase, consumed only while OPEN — and a chat row menu floating on top goes
  // first (its own capture closer swallows the key).
  const pressed = [];
  m.doc.addEventListener('keydown', (e) => pressed.push(e.key));
  const esc = m.doc.key('Escape');
  assert.ok(esc.stopped && !m.isOpen() && !pressed.length, 'Escape closes the open menu without leaking');
  assert.ok(!m.doc.key('Escape').stopped && pressed.length === 1, 'a closed menu leaves Escape alone');
  const { wireChatRowMenu } = await import('../../../js/ui/chat/view.js');
  const transcript = m.doc.body.appendChild(m.doc.createElement('div'));
  const row = transcript.appendChild(m.doc.createElement('div'));
  Object.assign(row, { className: 'chat-msg chat-msg-user', _chatRow: { id: 1, role: 'user', text: 'hi' } });
  wireChatRowMenu(transcript, {});
  m.open();
  transcript.dispatch('contextmenu', { target: row, clientX: 5, clientY: 5 });
  m.doc.key('Escape');
  assert.ok(m.isOpen(), 'the chat row menu on top takes Escape first');
});

test('assistantEnabled gates on the provider only', async (t) => {
  assert.strictEqual(assistantEnabled({ provider: 'none' }), false);
  assert.strictEqual(assistantEnabled(null), false);
  assert.strictEqual(assistantEnabled(undefined), false);
  for (const p of ['ollama', 'openai-compat', 'stencil-server']) {
    assert.strictEqual(assistantEnabled({ provider: p }), true, `${p} enables the entry`);
  }
  // A configured-but-unreachable provider still gets the entry (the failure shows
  // up in the reply, like the panel) — the gate never probes the endpoint.
  const m = await mountContextMenu(t);
  assert.strictEqual(m.$('ctx-assist-menu'), null, 'no provider, no entry');
  m.store.set('drawingApp_llmSettings', JSON.stringify({ provider: 'openai-compat', baseUrl: 'http://10.255.255.1:1' }));
  publish(EVENTS.llmSettingsChanged);
  assert.ok(m.$('ctx-assist-menu'), 'syncAssistant gates on the saved settings, with no probe in the way');
  m.store.set('drawingApp_llmSettings', JSON.stringify({ provider: 'none' }));
  m.open();
  assert.strictEqual(m.$('ctx-assist-menu').style.display, 'none', 're-evaluated per open');
});

test('the assistant entry never touches the static menu markup (built by syncAssistant)', () => {
  // The static menu is byte-identical whatever the provider — the entry is built at
  // wire()/open time, so gating it on/off can never disturb the original menu.
  const off = layoutWith({ provider: 'none' });
  const on = layoutWith({ provider: 'ollama', baseUrl: 'http://localhost:11434' });
  assert.strictEqual(on, off, 'static markup is provider-independent');
  for (const id of ASSIST_IDS) assert.ok(!off.includes(`id="${id}"`), `${id} absent from the static markup`);
  const seps = (m) => m.split('class="ctx-sep"').length - 1;
  // Anchored on the LAST item of the view group, so the slice holds only the boundary and not
  // the Image/Layout submenu's own internal separator.
  const gap = (m) => m.slice(m.indexOf('id="ctx-fullscreen"'), m.indexOf('id="ctx-draw-toggle"'));
  assert.strictEqual(seps(gap(off)), 1, 'one separator between the two groups');
  // …and the built entry carries no separator of its own, so nothing can dangle.
  assert.strictEqual(assistantItemHtml().split('class="ctx-sep"').length - 1, 0);
});
