// The rules both skins' tables share: a picture as same-colour runs along its rows, and the word as closed,
// locked, filled lines across the top of an image. DOM-free; desktop twin: support/skin/skinTable.hpp.

// Same-colour runs along each row: fewer rects, the same pixels. `colorAt(cx, cy, grid)` names a cell's colour.
export const colorRuns = (grid, colorAt) => {
  const runs = [];
  for (let cy = 0; cy < grid.rows; cy++) {
    let start = 0, color = colorAt(0, cy, grid);
    for (let cx = 1; cx <= grid.cols; cx++) {
      const c = cx < grid.cols ? colorAt(cx, cy, grid) : null;
      if (c === color) continue;
      runs.push({ x: start, y: cy, w: cx - start, h: 1, color });
      start = cx;
      color = c;
    }
  }
  return runs;
};

const round2 = (v) => Math.round(v * 100) / 100;

// `word`: { text, glyphs, grid: [gw, gh], gapCells }; `style`: { widthShare, centerYShare, colors, stroke, thickness }.
export const wordLinesOf = (w, h, word, style) => {
  const { text, glyphs, grid, gapCells } = word;
  const { widthShare, centerYShare, colors, stroke, thickness } = style;
  const [gw, gh] = grid;
  const n = text.length;
  const totalCells = n * gw + (n - 1) * gapCells;
  const cellPx = (w * widthShare) / totalCells;
  const left = (w - totalCells * cellPx) / 2;
  const top = h * centerYShare - (gh * cellPx) / 2;
  return [...text].map((ch, i) => ({
    points: glyphs[ch].map(([x, y]) => ({
      x: round2(left + (i * (gw + gapCells) + x) * cellPx),
      y: round2(top + y * cellPx),
    })),
    locked: true,
    color: stroke,
    fillColor: colors[i % colors.length],
    thickness,
  }));
};
