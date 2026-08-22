// One window at a time, and its own shortcut closes it.
//
// The bugs: a modal's shortcut re-opened the modal that was already showing (nothing appeared
// to happen), and opening a second window left the first one underneath it. Both are decided
// by wireModalShell, so they are testable without a real DOM — only classList, listeners and
// getElementById are exercised here.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from './helpers/dom.js';

const el = (id = '') => createStubElement('div', {
  id,
  getBoundingClientRect: () => ({ left: 0, top: 0, width: 10, height: 10, bottom: 10 }),
});

installDom({}, {
  window: { matchMedia: () => ({ matches: true }), innerWidth: 1000, innerHeight: 800 },
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
