// The multiple-windows mode (motionPrefs multiWindow): windows open side by side, a press outside
// closes none, the last one raised answers Escape, and a popover gesture and a click swap one
// window between its two shapes. Off, the shell keeps one window at a time.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const el = (id = '') => createStubElement('div', {
  id,
  getBoundingClientRect: () => ({ left: 0, top: 0, width: 10, height: 10, bottom: 10 }),
});

installDom({}, {
  window: {
    matchMedia: () => ({ matches: true }), innerWidth: 1000, innerHeight: 800,
    addEventListener() {}, removeEventListener() {},
  },
});

const { wireModalShell, closeOpenModal } = await import('../../../js/ui/base.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
const sideBySide = (on) => setMotionPrefs({ multiWindow: on });

test('side by side, opening a window leaves the others up, each on its own level', () => {
  sideBySide(true);
  const a = wireModalShell(el('mw-a'), null, null);
  const b = wireModalShell(el('mw-b'), null, null);
  a.open();
  b.open();
  assert.deepEqual([a.isOpen(), b.isOpen()], [true, true]);
  assert.ok(Number(b.raisedAt) > Number(a.raisedAt), 'the newer window is on top');
  document.dispatch('keydown', { key: 'Escape' });
  assert.deepEqual([a.isOpen(), b.isOpen()], [true, false], 'Escape takes the top window only');
  closeOpenModal();
  sideBySide(false);
});

test('side by side, a press on the empty overlay closes nothing; one at a time, it closes', () => {
  const overlay = el('mw-outside');
  const shell = wireModalShell(overlay, null, null);
  sideBySide(true);
  shell.open();
  overlay.dispatch('mousedown');
  assert.equal(shell.isOpen(), true, 'the app behind stays usable with the window up');
  sideBySide(false);
  overlay.dispatch('mousedown');
  assert.equal(shell.isOpen(), false);
});

test('side by side, the icon swaps a window between its full and popover shapes', () => {
  sideBySide(true);
  const overlay = el('mw-swap');
  const shell = wireModalShell(overlay, null, null);
  shell.toggle();
  assert.equal(shell.isPopover(), false);
  shell.openPopover(el());
  assert.deepEqual([shell.isOpen(), shell.isPopover()], [true, true], 'the full window became the popover');
  shell.toggle();
  assert.deepEqual([shell.isOpen(), shell.isPopover()], [true, false], 'a click grows it back into the window');
  shell.toggle();
  assert.equal(shell.isOpen(), false, 'and a click on an open window closes it');
  sideBySide(false);
});

test('one at a time, the shell keeps its single-window rules', () => {
  sideBySide(false);
  const a = wireModalShell(el('mw-one-a'), null, null);
  const b = wireModalShell(el('mw-one-b'), null, null);
  a.open();
  b.open();
  assert.deepEqual([a.isOpen(), b.isOpen()], [false, true]);
  b.openPopover(el());
  assert.equal(b.isPopover(), false, 'a popover gesture on an open window does nothing');
  b.close();
});

test('switched off, the windows collapse to one: the focused one, else the one on top', async () => {
  const { collapseToOneWindow } = await import('../../../js/ui/modal/registry.js');
  sideBySide(true);
  const a = wireModalShell(el('mw-col-a'), null, null);
  const b = wireModalShell(el('mw-col-b'), null, null);
  const c = wireModalShell(el('mw-col-c'), null, null);
  a.open(); b.open(); c.open();
  b.open();   // raised again: on top now
  collapseToOneWindow({ activeElement: null });
  assert.deepEqual([a.isOpen(), b.isOpen(), c.isOpen()], [false, true, false], 'the console keeps the top window');
  a.open(); b.open();
  const field = {};
  a.contains = (x) => x === field;
  collapseToOneWindow({ activeElement: field });
  assert.deepEqual([a.isOpen(), b.isOpen()], [true, false], 'the Visuals window whose box was unticked stays');
  a.close();
  sideBySide(false);
});
