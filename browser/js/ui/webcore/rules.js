// The webcore skin's table (config/webcore.json) and the pure rules over it: the picture's
// cells and the word's lines. DOM-free; desktop twin: support/webcore/rules.{hpp,cpp}, value
// for value.
import WEBCORE_DATA from '../../config/webcore.json' with { type: 'json' };

export const WEBCORE = WEBCORE_DATA;
export const SKIN_ATTR = 'data-skin';
export const SKIN_NAME = 'webcore';
export const OFF_TOAST = WEBCORE.strings.off;
export const PROJECT_NAME = WEBCORE.strings.project;
export const IMAGE_NAME = WEBCORE.strings.imageName;

// The picture's cell grid: `horizon` is the row the hill's foot rests on.
export const cellGrid = () => {
  const { width, height, cell, horizonShare } = WEBCORE.image;
  const rows = Math.floor(height / cell);
  return { cell, cols: Math.floor(width / cell), rows, horizon: Math.round(rows * horizonShare) };
};

// The crest at column cx: a repeating parabola in integer arithmetic, so both painters agree.
export const hillTopCell = (cx, grid) => {
  const { ampCells, spanCells, offsetCells } = WEBCORE.image.hill;
  const u = (((cx - offsetCells) % spanCells) + spanCells) % spanCells;
  return grid.horizon - Math.floor((ampCells * 4 * u * (spanCells - u)) / (spanCells * spanCells));
};

export const skyBandAt = (cy, grid) => {
  const bands = WEBCORE.image.skyBands;
  return bands[Math.min(bands.length - 1, Math.floor((cy * bands.length) / grid.horizon))];
};

// Crest to foot in bands, a checker dither half a band deep at every edge.
export const grassShadeAt = (cx, cy, grid) => {
  const { grass, hill } = WEBCORE.image;
  const bandCells = Math.max(1, Math.floor((grid.rows - grid.horizon + hill.ampCells) / grass.length));
  const d = cy - hillTopCell(cx, grid) + ((cx + cy) & 1) * (bandCells >> 1);
  return grass[Math.min(grass.length - 1, Math.floor(d / bandCells))];
};

export const cellColorAt = (cx, cy, grid) =>
  (cy >= hillTopCell(cx, grid) ? grassShadeAt(cx, cy, grid) : skyBandAt(cy, grid));

// Every cloud block as {x, y, w, h, color} in cells: the ink first, the shade over it.
export const cloudBlocks = () => {
  const { clouds, cloudInk, cloudShade } = WEBCORE.image;
  const out = [];
  for (const c of clouds) {
    for (const [dx, dy, w, h] of c.blocks) out.push({ x: c.x + dx, y: c.y + dy, w, h, color: cloudInk });
    for (const [dx, dy, w, h] of c.shade || []) out.push({ x: c.x + dx, y: c.y + dy, w, h, color: cloudShade });
  }
  return out;
};

// Same-colour runs along each row: fewer rects, the same pixels.
export const cellRuns = (grid) => {
  const runs = [];
  for (let cy = 0; cy < grid.rows; cy++) {
    let start = 0, color = cellColorAt(0, cy, grid);
    for (let cx = 1; cx <= grid.cols; cx++) {
      const c = cx < grid.cols ? cellColorAt(cx, cy, grid) : null;
      if (c === color) continue;
      runs.push({ x: start, y: cy, w: cx - start, h: 1, color });
      start = cx;
      color = c;
    }
  }
  return runs;
};

// The colour the finished picture holds at pixel (px, py): the last cloud block over the cell.
export const pixelColorAt = (px, py) => {
  const grid = cellGrid();
  const cx = Math.floor(px / grid.cell), cy = Math.floor(py / grid.cell);
  let color = cellColorAt(cx, cy, grid);
  for (const b of cloudBlocks())
    if (cx >= b.x && cx < b.x + b.w && cy >= b.y && cy < b.y + b.h) color = b.color;
  return color;
};

const round2 = (v) => Math.round(v * 100) / 100;

// The word as closed, locked, filled lines across the sky of a w×h image, one colour each.
export const wordLines = (w, h) => {
  const { text, glyphs, grid, gapCells, widthShare, centerYShare, colors, stroke, thickness } = WEBCORE.word;
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
