// Popups over the chat panel (js/ui/view.js): a menu is announced on both edges and the
// jump pills stand down, the composer menu outranks them, and every popup declares its level.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { COMPONENTS_CSS } from '../../../helpers/css.js';
import { chatViewSource } from '../../../helpers/chatViewSource.js';
import { contextMenuSource } from '../../../helpers/contextMenuSource.js';
import { stubDom } from '../../../helpers/chatRowMenuRig.js';

test('a chat popup is announced on BOTH edges, and the pills stand down while it is open', async () => {
  stubDom();
  const { CHAT_POPUP_EVENT, chatPopupOpen, chatRowMenuOpen } = await import('../../../../js/ui/chat/row/chatRowMenu.js?menu-events');
  assert.strictEqual(CHAT_POPUP_EVENT, 'stencil:chat-popup');
  assert.strictEqual(chatPopupOpen(), false, 'nothing open to begin with');
  assert.strictEqual(chatRowMenuOpen(), false);
  const view = chatViewSource();
  // Announced when it opens…
  const open = view.slice(view.indexOf('const openChatRowMenu ='));
  assert.ok(/rowMenuEl = menu;\s*\n\s*rowMenuClose = close;\s*\n\s*announcePopup\(\);/.test(open),
    'the open path announces after the menu is live');
  // …and when it closes, AFTER rowMenuEl is cleared so a listener reads "closed".
  const close = view.slice(view.indexOf('  const close = () => {', view.indexOf('const openChatRowMenu =')));
  const clearAt = close.indexOf('rowMenuEl = null;');
  const announceAt = close.indexOf('announcePopup();');
  assert.ok(clearAt > -1 && announceAt > clearAt, 'closed state is visible before the event fires');
  // The panel reacts to both edges: an open popup stands the PILLS down (they are
  // never hidden by the row-overlap reason any more — that yields the TRIGGER instead).
  const panel = readFileSync(new URL('../../../../js/ui/chat/panel.js', import.meta.url), 'utf8');
  assert.ok(panel.includes('const standDown = chatPopupOpen();'));
  assert.ok(panel.includes('subscribe(CHAT_POPUP_EVENT, syncJumps);'));
  // …and standDown gates BOTH classes, so neither arrow can survive an open menu.
  assert.ok(/const up = [^\n]*!standDown;/.test(panel) && /const down = [^\n]*!standDown;/.test(panel));
  // Nothing latches: the only inputs are the live menu/pill state and the hovered
  // trigger, so a close restores whatever the scroll position deserves.
  const sync = panel.slice(panel.indexOf('const syncRowMenuLift = () => {'), panel.indexOf('transcript.addEventListener(\'mouseover\''));
  assert.ok(!/menuWasOpen|pillsHidden|wasStandDown/.test(sync),
    'no remembered hidden state to get stuck in — it is recomputed every time');
});

test('the flyout shares the one menu, so its menu moves the panel pills too', () => {
  const menu = contextMenuSource();
  const view = chatViewSource();
  // One module-level menu for both surfaces (the flyout's keep-open predicate reads it),
  // and the announcement is inside that shared open/close — not in a per-surface wrapper.
  assert.ok(view.includes('let rowMenuEl = null;'), 'one menu app-wide');
  assert.ok(menu.includes('chatRowMenuOpen()'), 'the flyout consults the same state');
  // Two edges for the row menu, and the composer menu funnels both of its own through
  // one setOpen — so every popup edge in the module announces.
  assert.strictEqual((view.match(/announcePopup\(\)/g) || []).length, 3);
  // The pills only exist in the panel, so the panel is the only listener needed.
  assert.ok(!/chat-jumps/.test(menu), 'the flyout has no pills of its own');
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
  const view = chatViewSource();
  // ONE toggle path, so "is it open" can never disagree with what is on screen.
  const wire = view.slice(view.indexOf('export const wireChatMoreMenu'), view.indexOf('export const chatComposerActionsHtml') + 1 || undefined);
  const body = view.slice(view.indexOf('export const wireChatMoreMenu'));
  assert.ok(body.includes('const setOpen = (on) => {'), 'both edges funnel through setOpen');
  assert.ok(body.includes('if (on) openComposerMenus.add(menu); else openComposerMenus.delete(menu);'));
  assert.ok(body.includes('const close = () => setOpen(false);'));
  assert.ok(body.includes('setOpen(menu.hidden);'), 'the toggle goes through it too');
  // Every close path — item click, outside pointerdown, Escape — is that same close.
  assert.ok(body.includes("for (const item of menu.querySelectorAll('.chat-more-item')) item.addEventListener('click', close);"));
  assert.ok(/pointerdown[\s\S]{0,120}close\(\)/.test(body));
  assert.ok(/Escape[\s\S]{0,60}close\(\)/.test(body));
  // …and the pills read one predicate covering row menu AND composer menus.
  assert.match(view, /export const chatPopupOpen = \(\) => !!rowMenuEl \|\| openComposerMenus\.size > 0;/);
  // Both surfaces wire a composer menu, so both feed the same accounting.
  const panel = readFileSync(new URL('../../../../js/ui/chat/panel.js', import.meta.url), 'utf8');
  const flyout = contextMenuSource();
  assert.match(panel, /wireChatMoreMenu\('chat'/);
  assert.match(flyout, /wireChatMoreMenu\('ctx-assist'/);
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
