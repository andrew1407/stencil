// The pictures the fruit, cat and pie skins put in place of accent letters, and the cat's
// walk: ports of cli/src/app/skin/pictures.zig and cat.zig over the shared table. FNV-1a
// stands in for Wyhash — the draw is random by design.
import SKINS from '../../../../../common/config/console/skins.json' with { type: 'json' };

const P = SKINS.pictures;
const CAT = SKINS.cat;

const fnv = (...nums) => {
  let h = 0x811c9dc5;
  for (const n of nums) for (let i = 0; i < 4; i++) { h ^= (n >>> (i * 8)) & 0xff; h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
};

const listOf = (name) => (name === 'meow' ? P.cats : name === 'pie' ? P.pies : P.fruits);

// The picture index for the letter at `col` of the row keyed `rowKey`, never `prev` again: the
// same on every repaint of a frame, a new one when that cell's beat comes round. `clock` is the
// skin clock (table.js createSkinClock).
export const pictureAt = (name, rowKey, col, prev, clock) => {
  const frame = clock.frame(), seed = clock.seed();
  const cell = fnv(seed ^ rowKey, col);
  const beat = Math.floor((frame + (cell % P.swapFrames)) / P.swapFrames);
  let h = fnv(cell, beat);
  if (name === 'pie') return h % 5 === 0 ? 1 : 0;
  const n = listOf(name).length;
  let i = h % n;
  if (prev === i) { h >>>= 8; i = (i + 1 + (h % (n - 1))) % n; }
  return i;
};

export const picture = (name, i) => listOf(name)[i];

const spot = (leg, span, rows, seed) => {
  const h = fnv(seed, leg);
  return { col: h % (span + 1), row: (h >>> 20) % Math.max(rows, 1) };
};
const approach = (a, b, steps) => (a < b ? Math.min(b, a + steps) : b + Math.max(0, a - b - steps));
const LOOKS = ['open', 'blink', 'left', 'right', 'up', 'down'];

// Where the cat is and how it looks this frame inside a width × rows frame: it walks a cell a
// step between random spots and glances about or blinks at rest.
export const catPose = (width, rows, clock) => {
  const frame = clock.frame(), seed = clock.seed();
  const style = CAT.styles[seed % CAT.styles.length];
  const span = Math.max(0, width - style[0][1]);
  const f = Math.floor(frame / CAT.stepFrames);
  const leg = Math.floor(f / CAT.legFrames), t = f % CAT.legFrames;
  const from = spot(leg, span, rows, seed), to = spot(leg + 1, span, rows, seed);
  const col = approach(from.col, to.col, t), row = approach(from.row, to.row, t);
  let look = 'open';
  if (col !== to.col) look = to.col > col ? 'right' : 'left';
  else if (row !== to.row) look = to.row > row ? 'down' : 'up';
  else {
    const h = fnv(seed + 1, leg);
    if (t + 3 >= CAT.legFrames && t + 1 < CAT.legFrames && h % 2 === 0) look = 'blink';
    else if (h % 3 === 0 && t % 12 >= 6) look = LOOKS[2 + ((h >>> 8) % 4)];
  }
  const [text, cols] = style[LOOKS.indexOf(look)];
  return { col: Math.min(col, Math.max(0, width - cols)), row, text, cols };
};
