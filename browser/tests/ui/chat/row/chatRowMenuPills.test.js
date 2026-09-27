// Popups over the chat panel (js/ui/chat/view.js): a menu is announced on both edges and the
// jump pills stand down, the composer menu outranks them, and every popup declares its level.
import { test } from 'node:test';
import assert from 'node:assert';
import { COMPONENTS_CSS } from '../../../helpers/css.js';
import { wireBothSurfaces, rowEl, rightClick, openRowMenu, clickRowItem } from '../../../helpers/chatSurfacesRig.js';
import { CHAT_POPUP_EVENT, chatPopupOpen, chatRowMenuOpen } from '../../../../js/ui/chat/row/chatRowMenu.js';
import { assistantItemHtml } from '../../../../js/ui/ctx/assistantItem.js';
import { subscribe } from '../../../../js/eventBus/appBus.js';

// Both surfaces wired, one settled row in the log, and the panel transcript scrolled mid-way.
const scrolledSurfaces = async () => {
  const s = await wireBothSurfaces();
  const row = s.session.appendChatRow({ role: 'assistant', text: 'hello' });
  Object.assign(s.panel.transcript, { scrollTop: 50, scrollHeight: 500, clientHeight: 200 });
  s.panel.transcript.fire('scroll');
  const jumps = s.doc.getElementById('chat-jumps');
  const pills = () => ['can-up', 'can-down'].filter((c) => jumps.classList.contains(c));
  const heard = [];
  subscribe(CHAT_POPUP_EVENT, () => heard.push([chatPopupOpen(), chatRowMenuOpen()]));
  return { ...s, row, pills, heard };
};

test('a chat popup is announced on BOTH edges, and the pills stand down while it is open', async () => {
  assert.strictEqual(CHAT_POPUP_EVENT, 'stencil:chat-popup');
  const s = await scrolledSurfaces();
  assert.strictEqual(chatPopupOpen(), false, 'nothing open to begin with');
  assert.deepStrictEqual(s.pills(), ['can-up', 'can-down']);
  rightClick(s.panel.transcript, rowEl(s.panel.transcript, s.row.id));
  // Announced once the menu is live, and on close only after it reads closed.
  assert.deepStrictEqual(s.heard, [[true, true]], 'the open path announces after the menu is live');
  assert.deepStrictEqual(s.pills(), [], 'an open popup stands BOTH pills down');
  clickRowItem(openRowMenu(), 'Insert into prompt');
  assert.deepStrictEqual(s.heard, [[true, true], [false, false]], 'closed state is visible before the event fires');
  assert.deepStrictEqual(s.pills(), ['can-up', 'can-down'], 'nothing latches: the scroll position decides again');
});

test('the flyout shares the one menu, so its menu moves the panel pills too', async () => {
  const s = await scrolledSurfaces();
  rightClick(s.flyout.transcript, rowEl(s.flyout.transcript, s.row.id));
  assert.deepStrictEqual(s.pills(), [], 'a menu opened in the flyout stands the panel pills down');
  assert.strictEqual(s.flyout.el._keepOpen(), true, 'the flyout consults the same state');
  rightClick(s.panel.transcript, rowEl(s.panel.transcript, s.row.id));
  const menus = s.doc.body.children.filter((c) => c.classList.contains('chat-row-menu'));
  assert.strictEqual(menus.length, 1, 'one menu app-wide: the panel\'s replaces the flyout\'s');
  clickRowItem(menus[0], 'Insert into prompt');
  assert.strictEqual(chatRowMenuOpen(), false);
  assert.ok(!assistantItemHtml().includes('chat-jumps'), 'the flyout has no pills of its own');
});

// The composer's "…" menu is IN-PANEL — absolute inside the composer, popping upward into the pills'
// corner — so it competes with them directly and must outrank them (user report).
test('the composer overflow menu outranks the jump pills in the panel\'s own stacking', () => {
  const css = COMPONENTS_CSS;
  const lvl = (re) => { const m = re.exec(css); return m ? Number(m[1]) : null; };
  const menuZ = lvl(/\.chat-more-menu \{[^}]*z-index: (\d+)/);
  const jumpsZ = lvl(/\.chat-jumps \{[^}]*z-index: (\d+)/);
  const cueZ = lvl(/\.chat-drop-cue \{[^}]*z-index: (\d+)/);
  assert.ok(menuZ && jumpsZ && cueZ);
  assert.ok(menuZ > jumpsZ, `the popup ${menuZ} must cover the affordance ${jumpsZ}`);
  assert.ok(menuZ > cueZ, 'and the composer drop cue');
  // It pops UPWARD out of the composer — which is why it reaches the pills at all.
  assert.match(css, /\.chat-more-menu \{[^}]*bottom: calc\(100% \+ 6px\)/);
  // Its wrap is the positioning context, so the level is scoped to the panel.
  assert.match(css, /\.chat-more-wrap \{ position: relative;/);
});

test('the composer menu joins the popup accounting on both edges', async () => {
  const s = await scrolledSurfaces();
  for (const prefix of ['chat', 'ctx-assist']) {
    const btn = s.doc.getElementById(`${prefix}-more-btn`);
    const menu = s.doc.getElementById(`${prefix}-more-menu`);
    const item = menu.querySelector('.chat-more-item');
    // Every close path — item click, outside pointerdown, Escape — rides the same toggle.
    for (const close of [() => item.fire('click'), () => s.doc.fire('pointerdown', { target: s.doc.body }),
      () => s.doc.fire('keydown', { key: 'Escape' })]) {
      s.heard.length = 0;
      btn.fire('click');
      assert.deepStrictEqual([menu.hidden, s.heard, s.pills()], [false, [[true, false]], []], `${prefix}: open is announced`);
      close();
      assert.deepStrictEqual([menu.hidden, s.heard.at(-1), s.pills()], [true, [false, false], ['can-up', 'can-down']],
        `${prefix}: so is every close`);
    }
    btn.fire('click');
    btn.fire('click');
    assert.strictEqual(menu.hidden, true, `${prefix}: the button toggles through the same path`);
    assert.strictEqual(chatPopupOpen(), false);
  }
});

// The audit of every popup that can cover the chat panel: a new popup has to be added here with a
// declared level, or this fails.
test('every chat-panel popup declares a level that clears the pills', () => {
  const css = COMPONENTS_CSS;
  const lvl = (re) => { const m = re.exec(css); return m ? Number(m[1]) : null; };
  const PANEL = lvl(/stencil-chat-panel \{[^}]*z-index: (\d+)/);
  const JUMPS = lvl(/\.chat-jumps \{[^}]*z-index: (\d+)/);
  // scope: 'body'  → its own stacking context above the panel; compare against PANEL
  // scope: 'panel' → inside the panel's context; compare against JUMPS
  const AUDIT = [
    { name: '.chat-row-menu (per-message ⋯ / right-click)', scope: 'body', z: lvl(/\.project-menu, \.chat-row-menu \{[^}]*z-index: (\d+)/) },
    { name: '.chat-status-tip (provider tooltip)', scope: 'body', z: lvl(/\.chat-status-tip \{[^}]*z-index: (\d+)/) },
    { name: '.chat-thumb-preview (attachment glance)', scope: 'body', z: lvl(/\.chat-thumb-preview \{[^}]*z-index: (\d+)/) },
    { name: '#app-tooltip (shared instant tooltip)', scope: 'body', z: lvl(/#app-tooltip \{[^}]*z-index: (\d+)/) },
    { name: '.chat-more-menu (composer ⋯)', scope: 'panel', z: lvl(/\.chat-more-menu \{[^}]*z-index: (\d+)/) },
    { name: '.accent-dd-menu.dd-portal (select dropdowns, portaled to body)', scope: 'body', z: lvl(/\.accent-dd-menu\.dd-portal \{[^}]*z-index: (\d+)/) },
  ];
  for (const p of AUDIT) {
    assert.ok(Number.isFinite(p.z), `${p.name} must declare a z-index`);
    if (p.scope === 'body') assert.ok(p.z > PANEL, `${p.name} (${p.z}) must clear the panel (${PANEL})`);
    else assert.ok(p.z > JUMPS, `${p.name} (${p.z}) must clear the jump pills (${JUMPS})`);
  }
  // The affordances that must stay UNDER every popup above.
  for (const [name, z] of [['.chat-jumps', JUMPS], ['.chat-drop-cue', lvl(/\.chat-drop-cue \{[^}]*z-index: (\d+)/)]]) {
    assert.ok(z < lvl(/\.chat-more-menu \{[^}]*z-index: (\d+)/), `${name} stays below in-panel popups`);
  }
  // Ask cards / result cards / attachment chips are flow content — no z-index, so they
  // can never join this contest. If one ever gains one, this catches it.
  for (const sel of ['.chat-ask {', '.chat-results {', '.chat-attach-chip {']) {
    const at = css.indexOf(sel);
    if (at === -1) continue;
    assert.ok(!/z-index/.test(css.slice(at, css.indexOf('}', at))), `${sel} must stay flow content`);
  }
});
