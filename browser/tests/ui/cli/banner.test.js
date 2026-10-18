// The stage's banner (js/ui/cli/banner.js): the core's rows cell by cell, borders accent and
// the mark yellow, the wordmark on the label row, a skin's mark and wordmark and its pictures.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubDocument, createStubElement } from '../../helpers/dom.js';
import { bannerCells, wordmarkCells, markOf, wordmarkOf, paintBanner } from '../../../js/ui/cli/banner.js';
import { logoBannerRows } from '../../../js/core/format/logoBanner.js';
import { createSkinClock } from '../../../js/ui/cli/skin/table.js';

test('the cells are the core rows, each role by column', () => {
  const cells = bannerCells(null, false);
  assert.equal(cells.length, logoBannerRows('S', false).length);
  assert.deepEqual(cells.map((r) => r.map((c) => c.ch).join('')), logoBannerRows('S', false));
  const body = cells[2];
  assert.equal(body[2].role, 'accent', 'the left border');
  assert.ok(body.some((c) => c.role === 'mark' && c.ch === '●'));
  assert.equal(body[body.length - 1].role, 'accent', 'the right border');
  assert.equal(markOf('pie'), 'P');
  assert.equal(markOf('bifrost'), 'M');
  assert.equal(wordmarkOf('bifrost'), 'M I D G A R D');
  assert.equal(wordmarkOf(null), 'S T E N C I L');
});

test('a letter-swapping skin draws pictures for the letters, the cat skin walks its cat across the frame', () => {
  const clock = createSkinClock();
  clock.start(0, 11);
  for (let i = 0; i < 3; i++) clock.tick();
  const fruit = wordmarkCells('fruit', clock);
  assert.ok(fruit.filter((c) => c.role === 'picture').length === 7);
  assert.ok(fruit.every((c) => c.ch === ' ' || c.role === 'picture'));
  const meow = bannerCells('meow', false, clock);
  const art = meow.slice(2, 8).map((r) => r.filter((c) => c.role === 'mark').map((c) => c.ch).join(''));
  assert.equal(art.filter(Boolean).length, 1, 'one row carries the cat');
  const catRow = meow.find((r) => r.some((c) => c.role === 'mark'));
  const face = art.find(Boolean);
  assert.ok(face.includes('｀'), 'seed 11 draws the face with the fullwidth backtick');
  const plainRow = meow.slice(2, 8).find((r) => r !== catRow);
  assert.equal(catRow.length, plainRow.length, 'the row keeps its cells: the wide glyph takes two, the one after it is empty');
  const at = catRow.findIndex((c) => c.ch === '｀');
  assert.equal(catRow[at + 1].ch, '', 'and that cell is the glyph\'s second half');
});

test('paintBanner writes one span per cell and the wordmark beside the label row', () => {
  const doc = createStubDocument();
  const pre = createStubElement('pre', { ownerDocument: doc });
  paintBanner(pre, { accent: '#7c3aed' });
  assert.equal(pre.children.length, 10);
  const label = pre.children[5];
  assert.ok(label.children.some((c) => c.className === 'cli-wordmark'));
  assert.equal(pre.children[2].children[2].style.color, '#7c3aed');
  const marks = () => pre.children.reduce((n, r) => n + r.children.filter((c) => c.className === 'cli-cell cli-mark').length, 0);
  const fullMarks = marks();
  paintBanner(pre, { accent: '#7c3aed', compact: true });
  assert.equal(pre.children.length, 10, 'the press frame keeps the full logo box (press.zig)');
  assert.ok(marks() < fullMarks, 'with the smaller icon centred in it');
  assert.ok(pre.children[5].children.some((c) => c.className === 'cli-wordmark'), 'and the wordmark untouched');
});
