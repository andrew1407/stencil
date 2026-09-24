// The logo stage's table and rules (js/ui/stageRules.js): every accent preset opens a show, a
// custom hex picks by value, a styled show needs its motion mode, and the heart fits.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import ACCENTS from '../../../js/config/accents.json' with { type: 'json' };
import {
  STAGE, SHOWS, SHOW_NAMES, TYPED_WORDS, effectOf, resolveShow, showStyle,
  bigLogoSize, minLogoSize, heartPoints, heartLine,
} from '../../../js/ui/logo/stageRules.js';

test('every accent preset opens a show, told apart by motion, and every name is an identifier', () => {
  for (const { key } of ACCENTS) {
    const owners = SHOW_NAMES.filter((n) => (SHOWS[n].accents || []).includes(key));
    assert.ok(owners.length >= 1, `${key} is owned by nobody`);
    // A colour two rows share is told apart by their motion mode, never by their order.
    if (owners.length > 1)
      assert.equal(new Set(owners.map((n) => SHOWS[n].motion || '')).size, owners.length,
                   `${key}: ${owners.join(', ')} open under the same motion mode`);
  }
  for (const name of SHOW_NAMES) assert.match(name, /^[a-z][A-Za-z0-9]*$/);
  assert.deepEqual(TYPED_WORDS, SHOW_NAMES.map((n) => n.toLowerCase()));
  for (const n of SHOW_NAMES) {
    const hex = SHOWS[n].customHex;
    if (hex) assert.equal(hex, hex.toLowerCase(), `${n}: customHex is lower-case`);
    if (SHOWS[n].motion) assert.ok(['particles', 'water', 'fire', 'none'].includes(SHOWS[n].motion));
  }
  assert.ok(STAGE.holdMs >= 1000 && STAGE.toast.length > 0);
  // The skin's row opens by word, by call, and by a hold on grey with the interface still.
  assert.equal(effectOf('webcore'), 'webcore');
  assert.deepEqual(SHOWS.webcore.accents, ['grey']);
  assert.equal(SHOWS.webcore.motion, 'none');
  assert.equal(SHOWS.webcore.customHex, undefined);
  assert.equal(showStyle('webcore', 'dust'), null, 'no stage, so no cloud');
});

test('a preset resolves by its row; a styled row needs its motion mode', () => {
  assert.equal(resolveShow('violet', null, 'slide'), 'neonOn');
  assert.equal(resolveShow('orange', null, 'none'), 'makeSomeSunshine');
  assert.equal(resolveShow('crimson', null, 'fire'), 'firework');
  assert.equal(resolveShow('crimson', null, 'particles'), null, 'fire accent under dust: nothing');
  assert.equal(resolveShow('sky', null, 'water'), 'waterShow');
  assert.equal(resolveShow('sky', null, 'fire'), null);
  assert.equal(resolveShow('bluegray', null, 'particles'), 'dustySpot');
  assert.equal(resolveShow('grey', null, 'slide'), null);
  assert.equal(resolveShow('grey', null, 'particles'), 'dustySpot', 'grey while the interface moves');
  assert.equal(resolveShow('grey', null, 'none'), 'webcore', 'grey with the interface still: the skin');
  assert.equal(resolveShow('grey', null, 'water'), null, 'a mode neither row names opens nothing');
  assert.equal(resolveShow('grass', null, 'none'), 'makeItSmall');
  assert.equal(resolveShow('brown', null, 'none'), 'punchToBloat');
  assert.equal(resolveShow('pink', null, 'none'), 'pinkVibe');
  assert.equal(resolveShow('nosuch', null, 'particles'), null);
});

test('a custom hex resolves by value: white follows, black escapes, anything else flies', () => {
  assert.equal(resolveShow('violet', '#ffffff', 'particles'), 'chaseMe');
  assert.equal(resolveShow('violet', ' #FFFFFF ', 'particles'), 'chaseMe', 'case and space do not matter');
  assert.equal(resolveShow('violet', '#000000', 'particles'), 'runaway');
  assert.equal(resolveShow('violet', '#123456', 'particles'), 'randomWalk');
  assert.equal(resolveShow('violet', '', 'particles'), 'neonOn', 'an empty custom is no custom');
});

test('a styled show wears its own cloud; every other one wears the style in use', () => {
  assert.equal(showStyle('firework', 'dust'), 'fire', 'its own beats the current one');
  assert.equal(showStyle('waterShow', null), 'water');
  assert.equal(showStyle('dustySpot', 'fire'), 'dust');
  assert.equal(showStyle('chaseMe', 'water'), 'water');
  assert.equal(showStyle('randomWalk', 'fire'), 'fire');
  assert.equal(showStyle('punchToBloat', 'dust'), 'dust', 'the still shows wear it too');
  assert.equal(showStyle('punchToBloat', 'water'), 'water');
  assert.equal(showStyle('makeItSmall', 'fire'), 'fire');
  // Particles off: no cloud anywhere, and the light does the whole show.
  assert.equal(showStyle('runaway', null), null);
  // Neon IS the light and sun its own ring of beams: no cloud however the particles are set.
  assert.equal(showStyle('neonOn', 'dust'), null);
  assert.equal(showStyle('makeSomeSunshine', 'dust'), null);
  assert.equal(showStyle('neonOn', null), null);
  assert.equal(showStyle('nosuch', 'dust'), null);
  assert.equal(effectOf('punchToBloat'), 'grow');
  assert.equal(effectOf('nosuch'), null);
});

test('the stage sizes are shares of the shorter side', () => {
  assert.equal(bigLogoSize(1000, 600), 396);
  assert.equal(minLogoSize(1000, 600), 60);
  assert.equal(bigLogoSize(400, 900), 264);
});

test('the heart fits the centred square, n points round, and the line is locked and filled', () => {
  const pts = heartPoints(400, 300, 64);
  assert.equal(pts.length, 64);
  const xs = pts.map((p) => p.x), ys = pts.map((p) => p.y);
  const w = Math.max(...xs) - Math.min(...xs), h = Math.max(...ys) - Math.min(...ys);
  const span = 300 * (1 - 2 * STAGE.pink.heartInsetShare);
  assert.ok(Math.abs(Math.max(w, h) - span) < 0.02, `inset from the short side (${w} x ${h})`);
  assert.ok(Math.abs((Math.max(...xs) + Math.min(...xs)) / 2 - 200) < 0.02, 'centred in x');
  assert.ok(Math.abs((Math.max(...ys) + Math.min(...ys)) / 2 - 150) < 0.02, 'centred in y');
  assert.ok(pts[0].y < pts[32].y, 'the notch is at the top, the tip at the bottom (y down)');
  assert.deepEqual(pts[0], { x: 200, y: 93.41 }, 'the notch');
  assert.deepEqual(pts[16], { x: 320, y: 100.91 }, 'the right shoulder');
  assert.deepEqual(pts[32], { x: 200, y: 258.41 }, 'the tip');
  assert.deepEqual(pts[48], { x: 80, y: 100.91 }, 'mirrored on the left');
  const line = heartLine(400, 300);
  assert.equal(line.locked, true);
  assert.equal(line.fillColor, STAGE.pink.heartFill);
  assert.equal(line.color, STAGE.pink.heartStroke);
  assert.equal(line.points.length, STAGE.pink.heartPoints);
});
