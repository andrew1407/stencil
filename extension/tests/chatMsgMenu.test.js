// Tests for src/lib/chatMsgMenu.js — the assistant transcript's per-message
// right-click menu: Copy message / Insert into prompt, plus Resend on
// the user's own turns. Driven with a stub document like the other DOM-adjacent
// suites (chatUi.test.js), plus source assertions on popup/assistant.js's wiring
// (real clipboard / send loop) the stub DOM cannot reach.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  msgMenuItems, appendToPrompt, clampMenuPosition, createMsgMenu,
  msgMenuBtnSide, createMsgMenuButton, menuTransformOrigin,
} from '../src/lib/chatMsgMenu.js';
import { ICONS } from '../src/lib/icons.js';

// ── Stub DOM ──
const stubEl = (tag = 'div') => {
  const el = {
    tag, className: '', textContent: '', type: '', hidden: false, style: {},
    children: [], handlers: {}, _html: '', attrs: {},
    setAttribute: (k, v) => { el.attrs[k] = v; },
    offsetWidth: 120, offsetHeight: 80,
    appendChild: (c) => { el.children.push(c); return c; },
    append: (...cs) => { el.children.push(...cs); },
    addEventListener: (t, fn) => { (el.handlers[t] || (el.handlers[t] = [])).push(fn); },
    click: () => { for (const fn of el.handlers.click || []) fn({}); },
    get innerHTML() { return el._html; },
    set innerHTML(v) { el._html = v; if (v === '') el.children = []; },
  };
  return el;
};
const stubDoc = () => {
  const doc = {
    listeners: { keydown: [] },
    // DOM semantics: re-adding the same fn+capture is a no-op.
    addEventListener: (t, fn, cap) => {
      const l = doc.listeners[t] || (doc.listeners[t] = []);
      if (!l.some((e) => e.fn === fn && e.cap === cap)) l.push({ fn, cap });
    },
    removeEventListener: (t, fn) => {
      doc.listeners[t] = (doc.listeners[t] || []).filter((e) => e.fn !== fn);
    },
    createElement: (tag) => stubEl(tag),
    createTextNode: (text) => ({ text }),
  };
  return doc;
};
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
  const menu = createMsgMenu({ doc: stubDoc(), renderIcon: (n) => { seen.push(n); return `<${n}>`; }, actions: {} });
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
  const menu = createMsgMenu({ doc: stubDoc(), actions: {} });
  assert.equal(menu.el.className, 'action-menu chat-msg-menu');
  assert.equal(menu.el.hidden, true);
  assert.equal(menu.isOpen(), false);
});

// ── Action dispatch ──

test('clicking an item closes the menu and hands the action the clicked message', () => {
  const copied = [];
  const menu = createMsgMenu({ doc: stubDoc(), actions: { copy: (t) => copied.push(t.text) } });
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
  const menu = createMsgMenu({ doc: stubDoc(), actions: { resend: (t) => { got = t; } } });
  const attachments = [{ image: { mediaType: 'image/png', data: 'aa' }, name: 'a.png' }];
  menu.openFor({ role: 'user', text: 'crop it', resendText: 'crop it', attachments },
    { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  menu.el.children[2].click();
  assert.equal(got.resendText, 'crop it');
  assert.equal(got.attachments, attachments);   // the SAME array — requeueable
});

test('an item with no handler still closes the menu instead of throwing', () => {
  const menu = createMsgMenu({ doc: stubDoc(), actions: {} });
  menu.openFor({ role: 'user', text: 'x' }, { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  menu.el.children[0].click();
  assert.equal(menu.isOpen(), false);
});

test('pressing the menu never clears a selection: its mousedown is inert', () => {
  const menu = createMsgMenu({ doc: stubDoc(), actions: {} });
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
  const btn = createMsgMenuButton({ doc: stubDoc(), renderIcon: (n) => `<${n}>`, role: 'user' });
  assert.equal(btn.className, 'msg-menu-btn left');
  assert.equal(btn.type, 'button');
  assert.equal(btn.innerHTML, '<dots>');   // the existing overflow glyph, no new icon
  assert.ok(ICONS.dots);
  assert.equal(btn.attrs['aria-label'], 'Message actions');   // labelled, never aria-hidden
  assert.ok(btn.handlers.mousedown?.length, 'inert mousedown keeps selections');
  assert.equal(btn.tabIndex, -1);
  const other = createMsgMenuButton({ doc: stubDoc(), role: 'assistant' });
  assert.equal(other.className, 'msg-menu-btn right');
});

test('the button click path opens the very same items as a right-click', () => {
  const menu = createMsgMenu({ doc: stubDoc(), actions: {} });
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

// ── Placement (popup.js placeMenu parity) ──

test('the menu sits at the pointer when it fits', () => {
  assert.deepEqual(
    clampMenuPosition({ x: 40, y: 50, size: { width: 100, height: 80 }, viewport: { width: 400, height: 600 } }),
    { left: 40, top: 50 });
});

test('the menu pulls back inside the right/bottom edges', () => {
  const pos = clampMenuPosition({ x: 390, y: 590, size: { width: 100, height: 80 }, viewport: { width: 400, height: 600 } });
  assert.deepEqual(pos, { left: 294, top: 514 });
});

test('the menu never leaves the top-left margin', () => {
  const pos = clampMenuPosition({ x: 0, y: 0, size: { width: 500, height: 700 }, viewport: { width: 400, height: 600 } });
  assert.deepEqual(pos, { left: 6, top: 6 });
});

test('openFor positions the element with the clamp', () => {
  const menu = createMsgMenu({ doc: stubDoc(), actions: {} });
  menu.openFor({ role: 'user', text: 'x' }, { x: 390, y: 10, viewport: { width: 400, height: 600 } });
  assert.equal(menu.el.style.left, '274px');   // 400 - 120 - 6
  assert.equal(menu.el.style.top, '10px');
});

// ── Open-pop animation origin (shared with popup.js placeMenu) ──

test('menuTransformOrigin is the click point relative to the clamped menu', () => {
  assert.equal(menuTransformOrigin({ x: 50, y: 70, left: 50, top: 70, size: { width: 120, height: 80 } }),
    '0px 0px');   // menu at the pointer: grows from its top-left corner
  assert.equal(menuTransformOrigin({ x: 390, y: 590, left: 294, top: 514, size: { width: 100, height: 80 } }),
    '96px 76px'); // clamped away: the origin stays under the cursor
});

test('the origin never leaves the menu box', () => {
  assert.equal(menuTransformOrigin({ x: 0, y: 0, left: 6, top: 6, size: { width: 100, height: 80 } }), '0px 0px');
  assert.equal(menuTransformOrigin({ x: 500, y: 700, left: 294, top: 514, size: { width: 100, height: 80 } }),
    '100px 80px');
});

test('openFor grows the pop from the pointer: transform-origin at the click point', () => {
  const menu = createMsgMenu({ doc: stubDoc(), actions: {} });
  menu.openFor({ role: 'user', text: 'x' }, { x: 390, y: 10, viewport: { width: 400, height: 600 } });
  assert.equal(menu.el.style.transformOrigin, '116px 0px');   // 390 - clamped left 274
});

// ── Escape dismissal (owned by the menu, open-scoped) ──

test('Escape closes the menu, stops the event, and detaches its listener', () => {
  const doc = stubDoc();
  const menu = createMsgMenu({ doc, actions: {} });
  menu.openFor({ role: 'user', text: 'x' }, { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  assert.equal(doc.listeners.keydown.length, 1);
  assert.equal(doc.listeners.keydown[0].cap, true);   // capture: beats the dialog/popup behind
  let prevented = 0, stopped = 0;
  const ev = (key) => ({ key, preventDefault: () => prevented++, stopPropagation: () => stopped++ });
  doc.listeners.keydown[0].fn(ev('a'));
  assert.equal(menu.isOpen(), true);                  // other keys pass through untouched
  assert.equal(prevented + stopped, 0);
  doc.listeners.keydown[0].fn(ev('Escape'));
  assert.equal(menu.isOpen(), false);
  assert.equal(prevented, 1);
  assert.equal(stopped, 1);
  assert.equal(doc.listeners.keydown.length, 0);      // no leak after close
});

test('every close route detaches the keydown listener; reopen re-arms exactly one', () => {
  const doc = stubDoc();
  const menu = createMsgMenu({ doc, actions: {} });
  const at = { x: 0, y: 0, viewport: { width: 400, height: 600 } };
  menu.openFor({ role: 'user', text: 'x' }, at);
  menu.el.children[0].click();                        // item click closes
  assert.equal(doc.listeners.keydown.length, 0);
  menu.openFor({ role: 'user', text: 'x' }, at);
  menu.openFor({ role: 'assistant', text: 'y' }, at); // reopen while open: still one
  assert.equal(doc.listeners.keydown.length, 1);
  menu.close();
  assert.equal(doc.listeners.keydown.length, 0);
  menu.close();                                       // closing when closed stays safe
  assert.equal(doc.listeners.keydown.length, 0);
});

// ── assistant.js wiring (source assertions — the stub DOM cannot reach these) ──

const assistant = readFileSync(new URL('../src/popup/assistant.js', import.meta.url), 'utf8');
const css = readFileSync(new URL('../src/popup/popup.css', import.meta.url), 'utf8');

test('the transcript wires the menu on right-click, skipping notes and typing rows', () => {
  assert.match(assistant, /transcriptEl\.addEventListener\('contextmenu'/);
  assert.match(assistant, /closest\?\.\('\.msg'\)/);
  assert.match(assistant, /classList\.contains\('note'\) \|\| el\.classList\.contains\('typing-row'\)/);
});

test('copy uses the real clipboard', () => {
  assert.match(assistant, /navigator\.clipboard\?\.writeText\(t\.text\)/);
});

test('insert appends into the composer; resend rides the normal send loop', () => {
  assert.match(assistant, /insert: \(t\) => appendToPrompt\(inputEl, t\.text\)/);
  assert.match(assistant, /if \(!busy\) send\(t\.resendText, t\.attachments\)/);
});

test('the user turn records its original text + attachments for resend', () => {
  assert.match(assistant, /msgMeta\.set\(userEl, \{ text: text \|\| '\(attached images\)', resendText: text, attachments \}\)/);
});

test('the menu dismisses on outside press and transcript scroll; Escape is its own', () => {
  assert.match(assistant, /msgMenu\.isOpen\(\) && !msgMenu\.el\.contains\(e\.target\)\) msgMenu\.close\(\)/);
  // Escape moved INTO chatMsgMenu.js (open-scoped); no lingering document listener here.
  assert.doesNotMatch(assistant, /'Escape'\) msgMenu\.close\(\)/);
  assert.match(assistant, /transcriptEl\.addEventListener\('scroll', \(\) => msgMenu\.close\(\)\)/);
});

test('message text is selectable; the menu itself is not', () => {
  assert.match(css, /#sec-assistant \.msg \{[^}]*user-select: text/);
  assert.match(css, /\.chat-msg-menu \{[^}]*user-select: none/);
});

test('the message menu hugs its content; other action menus keep their floor', () => {
  assert.match(css, /\.chat-msg-menu \{[^}]*min-width: 0/);
  assert.match(css, /\.chat-msg-menu \{[^}]*width: max-content/);
  assert.match(css, /\.action-menu \{[^}]*min-width: 168px/);
});

test('every bubble (never notes) grows the hover trigger, wired through openMenuAt', () => {
  assert.match(assistant, /if \(cls !== 'note'\) addMsgMenuBtn\(el\);/);
  assert.match(assistant, /createMsgMenuButton\(\{/);
  assert.match(assistant, /openMenuAt\(el, e\.clientX, e\.clientY\)/);   // right-click, same path
  assert.match(assistant, /openMenuAt\(el, r\.left, r\.bottom \+ 2\)/);  // "⋯", anchored at its rect
});

test('the shared pop keyframe lives on .action-menu and respects reduced motion', () => {
  assert.match(css, /\.action-menu \{[^}]*animation: action-menu-pop \.13s ease-out/);
  assert.match(css, /@keyframes action-menu-pop \{\s*from \{ transform: scale\(\.62\); opacity: 0; \}/);
  assert.match(css, /@media \(prefers-reduced-motion: reduce\) \{\s*\.action-menu \{ animation: none; \}/);
});

const actionMenu = readFileSync(new URL('../src/lib/actionMenu.js', import.meta.url), 'utf8');

test('the shared action menu places via the origin helper; its Escape is armed per open', () => {
  assert.match(actionMenu, /menuEl\.style\.transformOrigin = menuTransformOrigin\(\{/);
  assert.match(actionMenu, /const armEscape = \(\) => doc\.addEventListener\('keydown', onEscape, true\)/);
  assert.match(actionMenu, /doc\.removeEventListener\('keydown', onEscape, true\)/);   // in close()
  assert.match(actionMenu, /e\.preventDefault\(\);\s*e\.stopPropagation\(\);\s*close\(\)/);  // stops the surface's own Escape
});

test('the trigger is an absolute ghost, revealed only for hover-capable pointers', () => {
  assert.match(css, /\.msg-menu-btn \{[^}]*position: absolute/);
  assert.match(css, /\.msg-menu-btn \{[^}]*opacity: 0/);
  assert.match(css, /\.msg-menu-btn \{[^}]*user-select: none/);
  assert.match(css, /#sec-assistant \.msg \{[^}]*position: relative/);   // the button's anchor
  assert.match(css, /@media \(hover: hover\) \{\s*#sec-assistant \.msg:hover \.msg-menu-btn \{[^}]*opacity: 1/);
  assert.match(css, /\.msg-menu-btn\.left \{ left: -24px; \}/);   // beside the bubble, no overlap
  assert.match(css, /\.msg-menu-btn\.right \{ right: -24px; \}/);
});
