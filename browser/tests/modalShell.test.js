// One window at a time, and its own shortcut closes it: a modal's shortcut must not re-open the modal already
// showing, and opening a second window must not leave the first one underneath it. Both are decided by
// wireModalShell, so only classList, listeners and getElementById are exercised here.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from './helpers/dom.js';

const el = (id = '') => createStubElement('div', {
  id,
  getBoundingClientRect: () => ({ left: 0, top: 0, width: 10, height: 10, bottom: 10 }),
});

installDom({}, {
  // addEventListener: the popover gesture machine (ui/popover.js) wires window listeners
  // whenever a shell has an opener button, which the popover test below needs.
  window: {
    matchMedia: () => ({ matches: true }), innerWidth: 1000, innerHeight: 800,
    addEventListener() {}, removeEventListener() {},
  },
});

const { wireModalShell, closeOpenModal } = await import('../js/ui/base.js');

test('a shell toggles: the same opener closes the window it opened', () => {
  const overlay = el('a-overlay');
  const shell = wireModalShell(overlay, null, null);
  shell.toggle();
  assert.equal(shell.isOpen(), true);
  shell.toggle();
  assert.equal(shell.isOpen(), false, 'the second press closes instead of re-opening');
});

test('opening a window closes whichever other one was showing', () => {
  const first = wireModalShell(el('first-overlay'), null, null);
  const second = wireModalShell(el('second-overlay'), null, null);
  first.open();
  assert.equal(first.isOpen(), true);

  second.open();
  assert.equal(second.isOpen(), true);
  assert.equal(first.isOpen(), false, 'the window it replaced is gone, not stacked under it');

  // …and the helper the hotkey layer uses reports what it closed.
  assert.equal(closeOpenModal(), second);
  assert.equal(closeOpenModal(), null, 'nothing left open');
});

test('a stacked shell opens OVER the window it was raised from', () => {
  const parent = wireModalShell(el('parent-overlay'), null, null);
  const stacked = wireModalShell(el('stacked-overlay'), null, null, { stacked: true });
  parent.open();
  stacked.open();
  assert.equal(stacked.isOpen(), true);
  assert.equal(parent.isOpen(), true, 'the window it was raised from stays up under it');

  // …and whatever opens next still clears BOTH of them.
  const other = wireModalShell(el('other-overlay'), null, null);
  other.open();
  assert.equal(parent.isOpen(), false);
  assert.equal(stacked.isOpen(), false, 'no window is left stranded under the new one');
  other.close();
});

test('Escape closes only the stacked window, not the one under it', () => {
  const under = wireModalShell(el('under-overlay'), null, null);
  const over = wireModalShell(el('over-overlay'), null, null, { stacked: true });
  under.open();
  over.open();

  document.dispatch('keydown', { key: 'Escape' });
  assert.equal(over.isOpen(), false, 'the top window answers the key');
  assert.equal(under.isOpen(), true, 'the one it was raised from is not taken with it');

  document.dispatch('keydown', { key: 'Escape' });
  assert.equal(under.isOpen(), false, '…and the next Escape closes it');
});

test('escapeClose: false keeps the FULL modal open, but its popover shape still closes', () => {
  // settingsModal owns Escape while a hotkey is being rebound, so the shell's own Escape
  // must leave the full window alone — the popover shape never rebinds, and always closes.
  const overlay = el('noesc-overlay');
  const shell = wireModalShell(overlay, null, null, { escapeClose: false });
  shell.open();
  document.dispatch('keydown', { key: 'Escape' });
  assert.equal(shell.isOpen(), true, 'the full modal keeps Escape for itself');
  shell.close();

  shell.open();
  overlay.classList.add('modal-popover');   // open() owns the class; the shape is set after
  document.dispatch('keydown', { key: 'Escape' });
  assert.equal(shell.isOpen(), false, 'the popover shape answers it');
  overlay.classList.remove('modal-popover');
});

// A window raised FROM a popover must not dismiss it: the click-outside rule reads the app's stacking order
// (components.css: overlays 100001, stacked editor 100002, portaled menus + confirm 100003).
test('a press in a layer raised over a popover leaves it open; the page below still closes it', () => {
  const zOf = new Map();
  globalThis.getComputedStyle = (node) => ({ zIndex: zOf.get(node) ?? 'auto' });

  const overlay = el('pop-overlay');
  const box = el('pop-box');
  overlay.appendChild(box);
  zOf.set(overlay, '100001');
  const openBtn = el('pop-btn');
  const shell = wireModalShell(overlay, openBtn, null);
  shell.openPopover(openBtn);
  assert.equal(shell.isOpen(), true);

  const press = (target) => document.dispatch('pointerdown', {
    target, preventDefault() {}, stopPropagation() {},
  });

  // Its own portaled menu, and a stacked window, both sit above it.
  const menu = el('raised-menu');
  const menuItem = el('raised-item');
  menu.appendChild(menuItem);
  zOf.set(menu, '100003');
  press(menuItem);
  assert.equal(shell.isOpen(), true, 'picking from the menu it raised must not close it');

  const stacked = el('stacked-overlay-2');
  zOf.set(stacked, '100002');
  press(stacked);
  assert.equal(shell.isOpen(), true, 'nor does working inside the window that menu opened');

  press(el('page-thing'));
  assert.equal(shell.isOpen(), false, '…but a press in the page below still dismisses it');
});
