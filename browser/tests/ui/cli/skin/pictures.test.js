// The skins' pictures and the cat (js/ui/cli/skin/pictures.js): no picture repeats its neighbour,
// a frame repaints the same, a picture swaps on its beat, pie is mostly pie, the cat keeps inside.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { pictureAt, picture, catPose } from '../../../../js/ui/cli/skin/pictures.js';

const at = (frame, seed) => ({ frame: () => frame, seed: () => seed });   // the skin clock's face

test('no picture repeats its neighbour, and a row repaints with the same ones', () => {
  for (const s of ['fruit', 'meow']) {
    let prev = null;
    for (let col = 0; col < 200; col++) {
      const i = pictureAt(s, 42, col, prev, at(0, 1));
      assert.notEqual(prev, i);
      assert.equal(i, pictureAt(s, 42, col, prev, at(0, 1)));
      prev = i;
    }
  }
  assert.equal(picture('meow', 0), '🐱');
});

test('a picture swaps on its own beat; pie mode is mostly pie with a mooncake through', () => {
  const first = pictureAt('fruit', 42, 10, null, at(0, 7));
  let changed = false;
  for (let f = 1; f < 83 * 8; f++) if (pictureAt('fruit', 42, 10, null, at(f, 7)) !== first) changed = true;
  assert.ok(changed);
  let pie = 0, cake = 0;
  for (let f = 0; f < 60; f++) for (let col = 0; col < 40; col++) (pictureAt('pie', 9, col * 2, null, at(f, 3)) === 0 ? pie++ : cake++);
  assert.ok(cake > 0 && pie > cake * 2);
});

test('the cat stays inside the frame and never moves more than a cell a frame', () => {
  let last = catPose(14, 6, at(0, 11));
  for (let f = 1; f < 2000; f++) {
    const p = catPose(14, 6, at(f, 11));
    assert.ok(p.col + p.cols <= 14 && p.row < 6);
    assert.ok(Math.abs(p.col - last.col) <= 2 && Math.abs(p.row - last.row) <= 1);
    last = p;
  }
});
