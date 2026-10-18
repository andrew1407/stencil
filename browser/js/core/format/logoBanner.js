// The console logo as rows of text: the favicon's rounded panel framing the signature
// polyline, rasterised from its vertices into a cell grid. Port of core/format/logoBanner.cpp,
// wasm-backed through core.bind('logoBanner'); the CLI stage paints these rows.
import { core } from '../abi/stencilCore.js';

export const MARKS = Object.freeze(['S', 'P', 'M']);

// The favicon vertices snapped into the 14×6 frame (cells are ~2:1 tall, so the bars land ON a
// row and the joins step one row at a time); the 10×5 ones are hand-snapped for the pressed frame.
const VERTS = Object.freeze({
  S: [[11, 0], [3, 0], [1, 1], [3, 2], [10, 2], [12, 3], [11, 5], [2, 5]],
  P: [[3, 5], [3, 0], [10, 0], [12, 1], [10, 2], [3, 2]],
  M: [[2, 5], [2, 0], [6, 3], [7, 3], [11, 0], [11, 5]],
});
const VERTS_SMALL = Object.freeze({
  S: [[8, 0], [2, 0], [1, 1], [2, 2], [7, 2], [8, 3], [7, 4], [1, 4]],
  P: [[2, 4], [2, 0], [7, 0], [8, 1], [7, 2], [2, 2]],
  M: [[1, 4], [1, 0], [4, 2], [5, 2], [8, 0], [8, 4]],
});

const GLYPH = Object.freeze({ space: ' ', up: '╱', down: '╲', mark: '●', flat: '─', vert: '│' });

export const logoBannerLayout = (compact) => {
  const frameWidth = compact ? 10 : 14;
  const margin = 1;
  return {
    indent: compact ? 4 : 2,
    frameWidth,
    frameHeight: compact ? 5 : 6,
    margin,
    panelWidth: frameWidth + 2 * margin,
    curveRows: compact ? 0 : 1,
    labelRow: Math.floor((compact ? 5 : 6) / 2),
  };
};

// Straight strokes between vertices (the glyph follows the segment's real slope: the S runs
// right-to-left across its bars, so a down-left join is ╱), a ● on each vertex.
const rasteriseMarkJS = (mark, compact) => {
  const l = logoBannerLayout(compact);
  const vs = (compact ? VERTS_SMALL : VERTS)[mark] ?? [];
  const g = Array.from({ length: l.frameHeight }, () => Array(l.frameWidth).fill(GLYPH.space));
  for (let s = 0; s + 1 < vs.length; s++) {
    const [ac, ar] = vs[s];
    const [zc, zr] = vs[s + 1];
    const downRight = (zr > ar) === (zc > ac);
    const stroke = zr === ar ? GLYPH.flat : zc === ac ? GLYPH.vert : downRight ? GLYPH.down : GLYPH.up;
    const dc = zc - ac;
    const dr = zr - ar;
    const steps = Math.max(Math.abs(dc), Math.abs(dr));
    for (let i = 1; i < steps; i++) {
      const t = i / steps;
      const cc = Math.round(ac + dc * t);
      const rr = Math.round(ar + dr * t);
      if (g[rr][cc] === GLYPH.space) g[rr][cc] = stroke;
    }
  }
  for (const [c, r] of vs) g[r][c] = GLYPH.mark;
  return g.map((row) => row.join(''));
};

const logoBannerRowsJS = (mark, compact) => {
  const l = logoBannerLayout(compact);
  const indent = ' '.repeat(l.indent);
  const margin = ' '.repeat(l.margin);
  const curveFill = ' '.repeat(l.panelWidth);
  const rule = '─'.repeat(compact ? l.panelWidth : l.panelWidth - 2);
  const inset = compact ? '' : ' ';
  const rows = [`${indent}${inset}╭${rule}╮`];
  if (!compact) rows.push(`${indent}╱${curveFill}╲`);
  for (const art of rasteriseMarkJS(mark, compact)) rows.push(`${indent}│${margin}${art}${margin}│`);
  if (!compact) rows.push(`${indent}╲${curveFill}╱`);
  rows.push(`${indent}${inset}╰${rule}╯`);
  return rows;
};

// `mark` is 'S' | 'P' | 'M'; an unknown mark gives no rows. `boxed` adds the rounded panel.
export const logoBanner = core.bind('logoBanner', (mark, compact, boxed) =>
  (MARKS.includes(mark) ? (boxed ? logoBannerRowsJS(mark, compact) : rasteriseMarkJS(mark, compact)) : []));

export const rasteriseMark = (mark, compact) => logoBanner(mark, compact, false);
export const logoBannerRows = (mark, compact) => logoBanner(mark, compact, true);
