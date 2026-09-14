import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { ctxKeyStep, CTX_NAV_KEYS, ctxFocusables } from '../js/ui/contextMenu.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';

// The context menu walks with the keyboard like the desktop's QMenu: ↑/↓ over the rows
// of the deepest open level, → opens a flyout, ← closes it, Enter/Space picks. The
// arrows used to fall through to the canvas pan (controlsBinder.js wireArrowPan), whose
// scroll then closed the menu — so "the arrows don't work in the menu".

test('ctxKeyStep wraps at both ends and starts from the first/last row when nothing is highlighted', () => {
  assert.strictEqual(ctxKeyStep(0, -1, 1), -1, 'no rows ⇒ nothing to land on');
  assert.strictEqual(ctxKeyStep(4, -1, 1), 0, 'down with no row ⇒ the first');
  assert.strictEqual(ctxKeyStep(4, -1, -1), 3, 'up with no row ⇒ the last');
  assert.strictEqual(ctxKeyStep(4, 1, 1), 2);
  assert.strictEqual(ctxKeyStep(4, 3, 1), 0, 'down from the last wraps to the first');
  assert.strictEqual(ctxKeyStep(4, 0, -1), 3, 'up from the first wraps to the last');
});

test('the open menu owns the arrow keys in the capture phase, so the pan never sees them', () => {
  const src = contextMenuSource();
  for (const k of ['ArrowDown', 'ArrowUp', 'ArrowLeft', 'ArrowRight', 'Enter', ' ']) assert.ok(CTX_NAV_KEYS.includes(k), k);
  // Registered as a capturing document listener, gated on the menu being open, and
  // stopping propagation — the pan binding is a bubbling document listener.
  const handler = src.slice(src.indexOf('if (!CTX_NAV_KEYS.includes(e.key)) return;'));
  const block = handler.slice(0, handler.indexOf('}, true);'));
  assert.ok(block.includes('e.preventDefault();') && block.includes('e.stopPropagation();'), 'consumed');
  assert.ok(src.includes("if (!menuIsOpen() || chatRowMenuOpen()) return;\n      if (!CTX_NAV_KEYS.includes(e.key)) return;"), 'only while open');
  // The assistant flyout's own text input keeps its keys (Enter sends there).
  assert.ok(block.includes('if (isTypingTarget(e.target) && menu.contains(e.target)) return;'));
  // → on a parent row reveals its flyout the first time and ENTERS it the second (the
  // chat's text box, else the first control, else the first row); ← folds the deepest
  // flyout back onto its parent row; Enter does the same on a parent, picks elsewhere.
  assert.ok(block.includes("ArrowRight: () => { if (kbItem && idx >= 0) openOrEnter(); },"));
  assert.ok(block.includes("if (sub.classList.contains('ctx-sub-visible')) kbEnterSub(sub);\n        else kbOpenSub(kbItem);"));
  assert.ok(block.includes("ArrowLeft: () => kbCloseSub(),"));
  assert.ok(block.includes('if (!openOrEnter()) kbItem.click();'));
  assert.ok(src.includes("return controls.find((el) => el.tagName === 'TEXTAREA') || controls[0] || null;"), 'the chat lands in its text box');
  assert.ok(src.includes('setKbItem(item);   // revealed only'), 'the first → keeps the highlight on the parent row');
  // A focused control keeps its keys; only ← (outside a text field) folds the flyout.
  assert.ok(block.includes("if (isTypingTarget(e.target)) return;\n        if (e.key.startsWith('Arrow')) e.stopPropagation();"), 'a text field keeps every key');
  assert.ok(block.includes("if (e.key === 'ArrowLeft') { e.preventDefault(); kbCloseSub(); }"));
});

test('the keyboard row wears the hover look, and the pointer takes it back on a real move', () => {
  const css = COMPONENTS_CSS;
  assert.match(css, /\.ctx-item:hover, \.ctx-item\.ctx-open-sub, \.ctx-item\.ctx-kb \{\s*background: var\(--bg-coord-hover\);/);
  const src = contextMenuSource();
  assert.ok(src.includes('if (moved) setKbItem(null);'), 'a pointer move clears the keyboard highlight');
  // Opening and closing the menu both start clean, and so does a Tab onto a control.
  assert.strictEqual(src.split('setKbItem(null);').length - 1, 5, 'cleared on open, close, pointer move, Tab and entering a control');
});

test('Tab walks the open flyout\'s own controls, wrapping, and only falls back to the rows without any', () => {
  assert.ok(CTX_NAV_KEYS.includes('Tab'), 'Tab is owned by the open menu');
  const src = contextMenuSource();
  // Controls first (a flyout with spinners/inputs/checkboxes), BEFORE the typing-target
  // exemption — Tab must leave the chat input for its buttons, not stay stuck in it.
  const tab = src.indexOf("if (e.key === 'Tab' && !ctxKeepsTab(document.activeElement)) {");
  const typing = src.indexOf('if (isTypingTarget(e.target) && menu.contains(e.target)) return;');
  assert.ok(tab > 0 && tab < typing, 'the Tab walk runs before the typing-target exemption');
  assert.ok(src.includes("controls[ctxKeyStep(controls.length, at, e.shiftKey ? -1 : 1)].focus();"), 'wraps via ctxKeyStep');
  assert.ok(src.includes("Tab: () => step(e.shiftKey ? -1 : 1),"), 'no controls ⇒ Tab walks the rows');
  // The control list is the flyout's live form controls: hidden ones are skipped.
  const level = {
    querySelectorAll: () => [
      { disabled: false, getClientRects: () => [1] },
      { disabled: true, getClientRects: () => [1] },
      { disabled: false, getClientRects: () => [] },
    ],
  };
  assert.strictEqual(ctxFocusables(level).length, 1, 'enabled + rendered only');
});

test('the keyboard row plays the hover motion too: icon, keycap shake and the content nudge', () => {
  const anim = ANIMATIONS_CSS;
  const comp = COMPONENTS_CSS;
  assert.ok(anim.includes('.ctx-item:not(.is-loading):not(.swapping):not([id^="toggle-"]).ctx-kb\n    > :is(.ctx-icon, .ctx-check)'), 'icon motion on .ctx-kb');
  assert.ok(anim.includes('.ctx-item:hover, .ctx-item.ctx-kb { padding-left: 17px; padding-right: 11px; }'), 'the row nudge');
  assert.ok(comp.includes('.ctx-item.ctx-kb > .ctx-hotkey .tip-key'), 'the keycap shake');
});
