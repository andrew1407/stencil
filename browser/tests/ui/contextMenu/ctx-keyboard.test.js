import { test } from 'node:test';
import assert from 'node:assert';
import { ctxKeyStep, CTX_NAV_KEYS, ctxFocusables } from '../../../js/ui/contextMenu/contextMenu.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from '../../helpers/css.js';
import { mountContextMenu } from '../../helpers/ctxMenuMountRig.js';

const ASSIST = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
// ↓ until the highlight lands on `id`'s row.
const walkTo = (m, id) => { for (let i = 0; i < 20 && m.kb()[0]?.id !== id; i++) m.doc.key('ArrowDown'); };

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

test('the open menu owns the arrow keys in the capture phase, so the pan never sees them', async (t) => {
  for (const k of ['ArrowDown', 'ArrowUp', 'ArrowLeft', 'ArrowRight', 'Enter', ' ']) assert.ok(CTX_NAV_KEYS.includes(k), k);
  const m = await mountContextMenu(t, { settings: ASSIST });
  const { doc, $ } = m;
  // A capturing document listener, gated on the menu being open, stopping propagation — the
  // pan binding is a bubbling document listener.
  const panned = [];
  doc.addEventListener('keydown', (e) => panned.push(e.key));
  assert.strictEqual(doc.key('ArrowDown').prevented, false, 'only while open');
  m.open();
  const down = doc.key('ArrowDown');
  assert.ok(down.prevented && down.stopped, 'consumed');
  assert.deepStrictEqual(panned, ['ArrowDown'], 'the pan saw the closed menu\'s key, never the open one\'s');
  assert.strictEqual(m.kb().length, 1, 'and a row wears the highlight');
  // The assistant flyout's own text input keeps its keys (Enter sends there).
  $('ctx-assist-input').focus();
  assert.strictEqual(doc.key('Enter').prevented, false, 'a text field in the menu keeps Enter');
  $('ctx-assist-input').blur();
  // → on a parent row reveals its flyout and, the second time, ENTERS it at the chat's text box.
  walkTo(m, 'ctx-assist-menu');
  doc.key('ArrowRight');
  assert.ok(m.visible('ctx-assist-sub'), 'the first → reveals');
  assert.strictEqual(m.kb()[0]?.id, 'ctx-assist-menu', '…and keeps the highlight on the parent row');
  doc.key('ArrowRight');
  assert.ok(doc.activeElement === $('ctx-assist-input'), 'the chat lands in its text box');
  // A text field keeps every key; a focused control keeps its keys but ← folds the flyout.
  assert.strictEqual(doc.key('ArrowLeft').stopped, false, 'a text field keeps ←');
  $('ctx-assist-send').focus();
  const along = doc.key('ArrowDown');
  assert.ok(along.stopped && !along.prevented, 'a control keeps ↓, but the pan never sees it');
  const back = doc.key('ArrowLeft');
  assert.ok(back.prevented && !m.visible('ctx-assist-sub'), '← folds the flyout back');
  assert.strictEqual(m.kb()[0]?.id, 'ctx-assist-menu', 'onto its parent row');
  // Enter does the same on a parent, and activates a plain row.
  doc.key('Enter');
  assert.ok(m.visible('ctx-assist-sub'), 'Enter opens the parent');
  doc.key('ArrowLeft');
  walkTo(m, 'ctx-fit-window');
  doc.key('Enter');
  assert.strictEqual($('ctx-fit-window').clicks, 1, 'and clicks a plain row');
});

test('the keyboard row wears the hover look, and the pointer takes it back on a real move', async (t) => {
  const css = COMPONENTS_CSS;
  assert.match(css, /\.ctx-item:hover, \.ctx-item\.ctx-open-sub, \.ctx-item\.ctx-kb \{\s*background: var\(--bg-coord-hover\);/);
  const m = await mountContextMenu(t, { settings: ASSIST });
  const lit = () => { m.doc.key('ArrowDown'); assert.strictEqual(m.kb().length, 1); };
  m.open(50, 60);
  lit();
  m.pointer(50, 60);
  assert.strictEqual(m.kb().length, 1, 'a still pointer leaves the highlight');
  m.pointer(51, 60);
  assert.strictEqual(m.kb().length, 0, 'a pointer move clears the keyboard highlight');
  // Opening and closing the menu both start clean, and so do Tab and entering a control.
  lit();
  m.open(51, 60);
  assert.strictEqual(m.kb().length, 0, 'cleared on open');
  lit();
  m.doc.key('Escape');
  assert.strictEqual(m.kb().length, 0, 'cleared on close');
  m.open(51, 60);
  walkTo(m, 'ctx-assist-menu');
  m.doc.key('ArrowRight');
  assert.strictEqual(m.kb().length, 1, 'a revealed flyout keeps its parent lit');
  m.doc.key('ArrowRight');
  assert.strictEqual(m.kb().length, 0, 'cleared on entering a control');
  m.doc.key('Escape');
  m.$('ctx-assist-input').blur();
  m.open(51, 60);
  walkTo(m, 'ctx-assist-menu');
  m.doc.key('ArrowRight');
  assert.strictEqual(m.kb().length, 1);
  m.doc.key('Tab');
  assert.strictEqual(m.kb().length, 0, 'cleared on a Tab onto a control');
});

test('Tab walks the open flyout\'s own controls, wrapping, and only falls back to the rows without any', async (t) => {
  assert.ok(CTX_NAV_KEYS.includes('Tab'), 'Tab is owned by the open menu');
  const m = await mountContextMenu(t, { settings: ASSIST });
  const { doc, $ } = m;
  m.open();
  walkTo(m, 'ctx-assist-menu');
  doc.key('ArrowRight');
  doc.key('ArrowRight');
  // Controls first, BEFORE the typing-target exemption — Tab must leave the chat input for its
  // buttons, not stay stuck in it.
  const input = $('ctx-assist-input');
  const tab = doc.key('Tab');
  assert.ok(tab.prevented && doc.activeElement !== input, 'Tab leaves the text box');
  doc.key('Tab', { shiftKey: true });
  assert.ok(doc.activeElement === input, 'Shift+Tab steps back');
  const controls = ctxFocusables($('ctx-assist-sub'));
  controls[0].focus();
  doc.key('Tab', { shiftKey: true });
  assert.ok(doc.activeElement === controls.at(-1), 'wraps via ctxKeyStep');
  // A flyout with no controls ⇒ Tab walks its rows.
  doc.key('Escape');
  m.open();
  walkTo(m, 'ctx-layout-menu');
  doc.key('ArrowRight');
  doc.key('ArrowRight');
  const first = m.kb()[0];
  doc.key('Tab');
  assert.ok(first.parentElement.id === 'ctx-layout-sub' && m.kb()[0] !== first, 'no controls ⇒ Tab walks the rows');
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
