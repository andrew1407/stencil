// src/lib/msgMenu.js — the assistant transcript's per-message right-click menu: Copy message /
// Insert into prompt, plus Resend on the user's own turns. Driven with a stub document.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  msgMenuItems, appendToPrompt, createMsgMenu, msgMenuBtnSide, createMsgMenuButton,
} from '../../../src/lib/chat/msgMenu.js';
import { ICONS } from '../../../src/lib/icons.js';
import { stubDoc, stubEl } from '../../helpers/domStub.js';

// The menu measures itself to clamp; the shared stub is inert, so give it a size.
const msgDoc = () => stubDoc({ createElement: (t) => stubEl(t, { offsetWidth: 120, offsetHeight: 80 }) });

const labelOf = (btn) => btn.children[1].text;

// ── Menu composition per role ──

test('an assistant message offers copy / insert — and no resend', () => {
  assert.deepEqual(msgMenuItems('assistant').map((i) => i.id), ['copy', 'insert']);
});

test('a user message adds Resend after the shared items', () => {
  assert.deepEqual(msgMenuItems('user').map((i) => i.id), ['copy', 'insert', 'resend']);
  assert.equal(msgMenuItems('user').at(-1).label, 'Resend');
});

test('every menu item names a real icon glyph', () => {
  for (const item of msgMenuItems('user')) {
    assert.ok(ICONS[item.icon], `no ICONS['${item.icon}'] for the ${item.id} item`);
  }
});

test('openFor builds the role\'s items in order, glyph + label each', () => {
  const seen = [];
  const menu = createMsgMenu({ doc: msgDoc(), renderIcon: (n) => { seen.push(n); return `<${n}>`; }, actions: {} });
  menu.openFor({ role: 'user', text: 'hi' }, { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  assert.deepEqual(menu.el.children.map(labelOf), ['Copy message', 'Insert into prompt', 'Resend']);
  assert.deepEqual(seen, ['copy', 'pencil', 'send']);   // browser chatRowMenuItems glyphs
  assert.equal(menu.el.children[0].children[0].className, 'ic');   // the popup menu's markup
  assert.equal(menu.el.children[0].type, 'button');

  // Re-opening on an assistant message rebuilds — the resend item is gone.
  menu.openFor({ role: 'assistant', text: 'yo' }, { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  assert.deepEqual(menu.el.children.map(labelOf), ['Copy message', 'Insert into prompt']);
});

test('the menu element carries the popup menu styling and boots closed', () => {
  const menu = createMsgMenu({ doc: msgDoc(), actions: {} });
  assert.equal(menu.el.className, 'action-menu chat-msg-menu');
  assert.equal(menu.el.hidden, true);
  assert.equal(menu.isOpen(), false);
});

// ── Action dispatch ──

test('clicking an item closes the menu and hands the action the clicked message', () => {
  const copied = [];
  const menu = createMsgMenu({ doc: msgDoc(), actions: { copy: (t) => copied.push(t.text) } });
  const target = { role: 'assistant', text: 'the model said this' };
  menu.openFor(target, { x: 10, y: 10, viewport: { width: 400, height: 600 } });
  assert.equal(menu.isOpen(), true);
  menu.el.children[0].click();   // Copy message
  assert.deepEqual(copied, ['the model said this']);
  assert.equal(menu.isOpen(), false);
  assert.equal(menu.el.children.length, 0);   // emptied, not just hidden
});

test('Resend dispatches the user turn — same text, same attachments', () => {
  let got = null;
  const menu = createMsgMenu({ doc: msgDoc(), actions: { resend: (t) => { got = t; } } });
  const attachments = [{ image: { mediaType: 'image/png', data: 'aa' }, name: 'a.png' }];
  menu.openFor({ role: 'user', text: 'crop it', resendText: 'crop it', attachments },
    { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  menu.el.children[2].click();
  assert.equal(got.resendText, 'crop it');
  assert.equal(got.attachments, attachments);   // the SAME array — requeueable
});

test('an item with no handler still closes the menu instead of throwing', () => {
  const menu = createMsgMenu({ doc: msgDoc(), actions: {} });
  menu.openFor({ role: 'user', text: 'x' }, { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  menu.el.children[0].click();
  assert.equal(menu.isOpen(), false);
});

test('pressing the menu never clears a selection: its mousedown is inert', () => {
  const menu = createMsgMenu({ doc: msgDoc(), actions: {} });
  let prevented = false;
  for (const fn of menu.el.handlers.mousedown) fn({ preventDefault: () => { prevented = true; } });
  assert.equal(prevented, true);
});

// ── Hover "⋯" trigger ──

test('the trigger sits on the side facing the panel centre', () => {
  assert.equal(msgMenuBtnSide('user'), 'left');        // user bubbles are right-aligned
  assert.equal(msgMenuBtnSide('assistant'), 'right');  // assistant/error bubbles left-aligned
  assert.equal(msgMenuBtnSide(''), 'right');
});

test('createMsgMenuButton builds the ghost dots button for the role', () => {
  const btn = createMsgMenuButton({ doc: msgDoc(), renderIcon: (n) => `<${n}>`, role: 'user' });
  assert.equal(btn.className, 'msg-menu-btn left');
  assert.equal(btn.type, 'button');
  assert.equal(btn.innerHTML, '<dots>');   // the existing overflow glyph, no new icon
  assert.ok(ICONS.dots);
  assert.equal(btn.attrs['aria-label'], 'Message actions');   // labelled, never aria-hidden
  assert.ok(btn.handlers.mousedown?.length, 'inert mousedown keeps selections');
  assert.equal(btn.tabIndex, -1);
  const other = createMsgMenuButton({ doc: msgDoc(), role: 'assistant' });
  assert.equal(other.className, 'msg-menu-btn right');
});

test('the button click path opens the very same items as a right-click', () => {
  const menu = createMsgMenu({ doc: msgDoc(), actions: {} });
  const target = { role: 'user', text: 'hi' };
  const vp = { viewport: { width: 400, height: 600 } };
  menu.openFor(target, { x: 15, y: 25, ...vp });   // right-click: at the pointer
  const viaContext = menu.el.children.map(labelOf);
  // The "⋯" button anchors the SAME openFor at its own rect instead.
  const rect = { left: 30, bottom: 90 };
  menu.openFor(target, { x: rect.left, y: rect.bottom + 2, ...vp });
  assert.deepEqual(menu.el.children.map(labelOf), viaContext);
  assert.equal(menu.el.style.left, '30px');
  assert.equal(menu.el.style.top, '92px');
});

// ── Insert into prompt ──

test('appendToPrompt fills an empty composer and focuses it', () => {
  let focused = 0;
  const inputEl = { value: '', focus: () => { focused++; } };
  appendToPrompt(inputEl, 'make it sepia');
  assert.equal(inputEl.value, 'make it sepia');
  assert.equal(focused, 1);
});

test('appendToPrompt appends to a half-written draft on its own line', () => {
  const inputEl = { value: 'so far', focus: () => {} };
  appendToPrompt(inputEl, 'and this');
  assert.equal(inputEl.value, 'so far\nand this');
});
