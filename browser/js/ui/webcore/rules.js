// The webcore skin's table (config/webcore.json) and the pure rules over it: the picture's
// cells and the word's lines. DOM-free; desktop twin: support/webcore/rules.{hpp,cpp}, value
// for value.
import WEBCORE_DATA from '../../../../common/config/webcore.json' with { type: 'json' };
import { colorRuns, wordLinesOf } from '../skin/rules.js';

export const WEBCORE = WEBCORE_DATA;
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

// Same-colour runs along each row of the base picture.
export const cellRuns = (grid) => colorRuns(grid, cellColorAt);

// The word as closed, locked, filled lines across the sky of a w×h image, one colour each.
export const wordLines = (w, h) => wordLinesOf(w, h, WEBCORE.word, WEBCORE.word);
