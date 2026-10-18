// The skins' restyle of an accent run (js/ui/cli/skin/restyle.js, restyle.zig): what the accent
// becomes, which skins re-dress at all, the hue gradient's columns, and the pictures in place of
// letters — two cells each, never the same twice in a row, the same again on a repaint.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubDocument, createStubElement } from '../../../helpers/dom.js';
import { accentUnder, restyles, hueGradient, rowKey, swappable, fillRun, PICTURE_CLASS, HUE_PERIOD } from '../../../../js/ui/cli/skin/restyle.js';
import { createSkinClock, traitsOf, GOLD, hueAt, SKIN_NAMES } from '../../../../js/ui/cli/skin/table.js';

test('the accent turns the cell painters\' fg, the picture skins\' gold, and stays itself elsewhere', () => {
  assert.equal(accentUnder('matrix', '#7c3aed'), traitsOf('matrix').fg);
  assert.equal(accentUnder('bluescreen', '#7c3aed'), traitsOf('bluescreen').fg);
  for (const s of ['fruit', 'meow', 'pie']) assert.equal(accentUnder(s, '#7c3aed'), GOLD);
  for (const s of ['rainbow', 'bifrost', 'fairylight', null]) assert.equal(accentUnder(s, '#7c3aed'), '#7c3aed');
  for (const s of SKIN_NAMES) assert.equal(restyles(s), s !== 'fairylight', `${s} re-dresses the rows (skin.zig restyles)`);
  assert.equal(restyles(null), false);
});

test('the hue gradient has a hard stop per column over the period the hues repeat on', () => {
  const g = hueGradient('rainbow');
  assert.ok(g.startsWith('repeating-linear-gradient(90deg, '));
  assert.equal(g.split(', rgb').length - 1, HUE_PERIOD);
  assert.ok(g.includes(`${hueAt('rainbow', 5, 0)} 5ch 6ch`));
  assert.equal(hueAt('rainbow', HUE_PERIOD, 0), hueAt('rainbow', 0, 0), 'the period closes the wheel');
});

test('a row key is stable and tells rows apart; printable ASCII swaps, spaces and box glyphs stay', () => {
  assert.equal(rowKey('/open'), rowKey('/open'));
  assert.notEqual(rowKey('/open'), rowKey('/save'));
  assert.ok(swappable('/') && swappable('a') && swappable('>'));
  assert.ok(!swappable(' ') && !swappable('━') && !swappable('\n'));
});

test('fillRun puts a picture two cells wide for every letter of a picture skin, the text back without one', () => {
  const doc = createStubDocument();
  const el = createStubElement('span', { ownerDocument: doc });
  const clock = createSkinClock();
  clock.start(0, 11);
  fillRun(el, '/open <name>', { skin: 'meow', clock, key: rowKey('/open <name>') });
  const pictures = el.children.filter((c) => c.className === PICTURE_CLASS);
  assert.equal(pictures.length, '/open<name>'.length, 'one picture per letter');
  assert.ok(el.children.some((c) => c.nodeType === 3 && c.nodeValue === ' '), 'the space stays text');
  const cats = traitsOf('meow') && pictures.map((p) => p.textContent);
  for (let i = 1; i < cats.length; i++) assert.notEqual(cats[i], cats[i - 1], 'never the same picture twice running');
  const again = createStubElement('span', { ownerDocument: doc });
  fillRun(again, '/open <name>', { skin: 'meow', clock, key: rowKey('/open <name>') });
  assert.deepEqual(again.children.map((c) => c.textContent ?? c.nodeValue), el.children.map((c) => c.textContent ?? c.nodeValue), 'a repaint draws the same row');
  fillRun(el, '/open <name>', { skin: 'rainbow', clock });
  assert.equal(el.children.length, 0);
  assert.equal(el.textContent, '/open <name>', 'the rainbow keeps the letters: its hues are the stylesheet\'s');
  fillRun(el, '/open', { skin: 'pie', clock: null });
  assert.equal(el.textContent, '/open', 'no clock, no pictures');
});
