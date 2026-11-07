// lib/accent/ink.js against the pre-paint copy in lib/prefs/prefs.js (window.StencilKit): the two
// ink rules agree hex for hex, as the browser's accents.js they both port.
import test from 'node:test';
import assert from 'node:assert/strict';
import { loadAccent } from '../../helpers/accentSandbox.js';
import { needsDarkGlyph, onAccentInk } from '../../../src/lib/accent/ink.js';

test('needsDarkGlyph and onAccentInk agree with the pre-paint copy on every accent and edge', () => {
  const { kit } = loadAccent();
  const hexes = [...kit.ACCENTS.map((a) => a.hex), '#000000', '#ffffff', '#fff', 'abc', ' #FFFF00 ', '#123', '', null, 'nope'];
  for (const hex of hexes) {
    assert.equal(kit.needsDarkGlyph(hex), needsDarkGlyph(hex), `needsDarkGlyph(${hex})`);
    assert.equal(kit.onAccentInk(hex), onAccentInk(hex), `onAccentInk(${hex})`);
  }
});
