// The open menu closes on a page scroll, but not on the one the browser makes to absorb a layout
// shift above the canvas (a toolbar row wrapping as it settles on a phone): that scroll leaves the
// canvas where it was on screen, so the menu is still over the point it opened at.
import { test } from 'node:test';
import assert from 'node:assert';
import { mountContextMenu } from '../../helpers/ctxMenuMountRig.js';

const scroll = (m) => m.doc.listeners('scroll').forEach((l) => l.fn({ target: null }));

test('a scroll that left the canvas in place keeps the menu; one that moved it closes it', async (t) => {
  let top = 120;
  const canvas = { getBoundingClientRect: () => ({ top, left: 0, width: 200, height: 150 }) };
  const m = await mountContextMenu(t, { app: { canvas } });
  m.open();
  assert.ok(m.isOpen());
  scroll(m);
  assert.ok(m.isOpen(), 'a layout shift absorbed by the browser: the canvas did not move');
  top = 60;
  scroll(m);
  assert.ok(!m.isOpen(), 'the user scrolled the page: the canvas moved under the menu');
});

test('without a canvas to measure, any page scroll still closes the menu', async (t) => {
  const m = await mountContextMenu(t);
  m.open();
  scroll(m);
  assert.ok(!m.isOpen());
});
