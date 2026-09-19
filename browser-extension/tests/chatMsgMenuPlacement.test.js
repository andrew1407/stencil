// Where src/lib/chatMsgMenu.js puts the menu: the clamp against the viewport edges, the pop's
// transform-origin under the cursor, and the Escape listener armed per open.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { clampMenuPosition, createMsgMenu, menuTransformOrigin } from '../src/lib/chatMsgMenu.js';
import { stubDoc, stubEl } from './helpers/domStub.js';

// The menu measures itself to clamp; the shared stub is inert, so give it a size.
const msgDoc = () => stubDoc({ createElement: (t) => stubEl(t, { offsetWidth: 120, offsetHeight: 80 }) });

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
  const menu = createMsgMenu({ doc: msgDoc(), actions: {} });
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
  const menu = createMsgMenu({ doc: msgDoc(), actions: {} });
  menu.openFor({ role: 'user', text: 'x' }, { x: 390, y: 10, viewport: { width: 400, height: 600 } });
  assert.equal(menu.el.style.transformOrigin, '116px 0px');   // 390 - clamped left 274
});

// ── Escape dismissal (owned by the menu, open-scoped) ──

test('Escape closes the menu, stops the event, and detaches its listener', () => {
  const doc = msgDoc();
  const menu = createMsgMenu({ doc, actions: {} });
  menu.openFor({ role: 'user', text: 'x' }, { x: 0, y: 0, viewport: { width: 400, height: 600 } });
  assert.equal(doc.on('keydown').length, 1);
  assert.equal(doc.on('keydown')[0].capture, true);   // capture: beats the dialog/popup behind
  let prevented = 0, stopped = 0;
  const ev = (key) => ({ key, preventDefault: () => prevented++, stopPropagation: () => stopped++ });
  doc.on('keydown')[0].fn(ev('a'));
  assert.equal(menu.isOpen(), true);                  // other keys pass through untouched
  assert.equal(prevented + stopped, 0);
  doc.on('keydown')[0].fn(ev('Escape'));
  assert.equal(menu.isOpen(), false);
  assert.equal(prevented, 1);
  assert.equal(stopped, 1);
  assert.equal(doc.on('keydown').length, 0);      // no leak after close
});

test('every close route detaches the keydown listener; reopen re-arms exactly one', () => {
  const doc = msgDoc();
  const menu = createMsgMenu({ doc, actions: {} });
  const at = { x: 0, y: 0, viewport: { width: 400, height: 600 } };
  menu.openFor({ role: 'user', text: 'x' }, at);
  menu.el.children[0].click();                        // item click closes
  assert.equal(doc.on('keydown').length, 0);
  menu.openFor({ role: 'user', text: 'x' }, at);
  menu.openFor({ role: 'assistant', text: 'y' }, at); // reopen while open: still one
  assert.equal(doc.on('keydown').length, 1);
  menu.close();
  assert.equal(doc.on('keydown').length, 0);
  menu.close();                                       // closing when closed stays safe
  assert.equal(doc.on('keydown').length, 0);
});

