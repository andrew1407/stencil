// config/console/skins.json is the asset: eight skins with unique words, each with a phrase or a
// screen, the marks named, the pictures and faces the browser port reads.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import SKINS from '../../../common/config/console/skins.json' with { type: 'json' };
import { skinOf, isSecret, SKIN_NAMES, LIST_WORD, GOLD } from '../../js/ui/cli/skin/table.js';

test('eight skins, each with a unique lower-case word, matched in any casing', () => {
  assert.equal(SKIN_NAMES.length, 8);
  const words = SKIN_NAMES.map((n) => SKINS.skins[n].word);
  assert.equal(new Set(words).size, 8);
  for (const w of words) assert.match(w, /^[a-z]+$/);
  assert.equal(skinOf('MRANDERSON'), 'matrix');
  assert.equal(skinOf('PieDay'), 'pie');
  assert.equal(skinOf('matrix'), null);
  assert.equal(isSecret('EasterEggs'), true);
  assert.equal(isSecret('help'), false);
  assert.equal(LIST_WORD, 'eastereggs');
  assert.equal(GOLD, '#ffd700');
});

test('every skin says something going on (or shows its screen), and the mark skins name their letter', () => {
  for (const n of SKIN_NAMES) {
    const t = SKINS.skins[n];
    assert.ok(t.phrase || t.body, `${n} has a phrase or a screen`);
    if (t.paintsCells) assert.ok(/^#[0-9a-f]{6}$/.test(t.fg) && /^#[0-9a-f]{6}$/.test(t.bg), `${n}: cell colours`);
  }
  assert.equal(SKINS.skins.pie.mark, 'P');
  assert.equal(SKINS.skins.bifrost.mark, 'M');
  assert.equal(SKINS.pictures.cats.length, 3);
  assert.equal(SKINS.cat.styles.length, 4);
  for (const style of SKINS.cat.styles) assert.equal(style.length, 6);
  assert.equal(SKINS.hues.bulbs.length, 6);
});
