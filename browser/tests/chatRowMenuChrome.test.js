// The chrome deltas (components.css): the chat menu hugs its content, wears the projects row
// menu's chrome, leaves messages selectable, and shares the app popup tier.
import { test } from 'node:test';
import assert from 'node:assert';
import { COMPONENTS_CSS } from './helpers/css.js';

// ── The chrome deltas ──
test('the chat menu hugs its content, and the "…" trigger is hover-gated CSS', () => {
  const css = COMPONENTS_CSS;
  const shared = css.indexOf('.project-menu, .chat-row-menu {');
  const override = css.indexOf('.chat-row-menu { min-width: 0; }');
  assert.ok(shared > -1 && override > shared,
    'the chat menu sheds the shared 184px floor AFTER the aliased rule (projects keep it)');
  assert.ok(css.includes('.chat-row-menu-item { padding: 7px 9px; }'), 'tighter items, chat only');
  // Per-side placement classes exist, and the reveal lives behind the hover guard.
  assert.ok(css.includes('.chat-row-menu-btn-left { left:'));
  assert.ok(css.includes('.chat-row-menu-btn-right { right:'));
  assert.ok(/@media \(hover: hover\)[^]*?\.chat-msg:hover \.chat-row-menu-btn/.test(css),
    'touch surfaces never see the hover trigger');
  // Anchored without moving the bubble, and never part of a text selection.
  const msg = css.slice(css.indexOf('\n.chat-msg {'), css.indexOf('}', css.indexOf('\n.chat-msg {')));
  assert.ok(msg.includes('position: relative'));
  const btn = css.slice(css.indexOf('.chat-row-menu-btn {'), css.indexOf('.chat-row-menu-btn-left'));
  assert.ok(btn.includes('position: absolute') && btn.includes('user-select: none'));
});

test('the row menu wears the projects row menu\'s exact chrome, and messages stay selectable', () => {
  const css = COMPONENTS_CSS;
  // Aliased selectors, not a copied block — the two menus can never drift apart.
  assert.ok(css.includes('.project-menu, .chat-row-menu {'), 'one floating-menu block');
  assert.ok(css.includes('.project-menu-item, .chat-row-menu-item {'), 'one item treatment');
  assert.ok(css.includes('.project-menu-item:hover, .chat-row-menu-item:hover'), 'same hover accent');
  // Theme vars only — both themes follow automatically.
  const block = css.slice(css.indexOf('.project-menu, .chat-row-menu {'), css.indexOf('.confirm-choose-row'));
  assert.ok(/var\(--bg-container\)/.test(block) && /var\(--accent\)/.test(block) && /var\(--text-main\)/.test(block));
  // The selection fix: message bubbles opt into selection EXPLICITLY (the ctx-menu
  // flyout lives inside a user-select:none ancestor).
  const msg = css.slice(css.indexOf('\n.chat-msg {'), css.indexOf('}', css.indexOf('\n.chat-msg {')));
  assert.ok(msg.includes('user-select: text'), 'message text is selectable');
  assert.ok(msg.includes('-webkit-user-select: text'), '…including WebKit');
});

// Two independent guarantees, because one number in a stylesheet is not a fix: an open menu STACKS
// above the jump pills, and the pills stand down while any menu is open (user report).
test('the row menu shares the app popup tier, which is above the panel and its pills', () => {
  const css = COMPONENTS_CSS;
  const tier = (re) => { const m = re.exec(css); return m ? Number(m[1]) : null; };
  // The chat menu is ALIASED onto the projects row menu — same block, same level.
  const shared = /\.project-menu, \.chat-row-menu \{[^}]*z-index: (\d+)/.exec(css);
  assert.ok(shared, 'the two row menus share one block');
  const menuZ = Number(shared[1]);
  // …the same tier the app's other popups use, not an ad-hoc number.
  assert.strictEqual(menuZ, tier(/#app-tooltip \{[^}]*z-index: (\d+)/), 'same tier as the tooltip');
  assert.strictEqual(menuZ, tier(/#confirm-modal-overlay\.modal-open \{ z-index: (\d+); \}/),
    'same tier as the confirm dialog');
  // The pills live INSIDE the chat panel, so the panel's own level is what the menu has
  // to clear — and it does, by three orders of magnitude.
  const panelZ = tier(/stencil-chat-panel \{[^}]*z-index: (\d+)/);
  const jumpsZ = tier(/\.chat-jumps \{[^}]*z-index: (\d+)/);
  assert.ok(panelZ && jumpsZ);
  assert.ok(menuZ > panelZ, `menu ${menuZ} must outrank the panel ${panelZ}`);
  assert.ok(panelZ > jumpsZ, 'the pills are scoped inside the panel, so the panel is the ceiling');
  // Fullscreen chrome is lower still, so the menu wins there too.
  for (const m of css.matchAll(/z-index: (100\d\d);/g)) assert.ok(Number(m[1]) < menuZ + 8);
  assert.ok(menuZ > 10002, 'above every fullscreen panel (10000-10002)');
});
