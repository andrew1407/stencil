// The S icon's clock turn, a port of cli/src/console/render/logoFx/clock.zig: a hand pivots on
// the icon's middle and sweeps the new accent round it while the seam takes the rest of the row.
import FX from '../../../../common/config/console/fx.json' with { type: 'json' };

const W = FX.wipe;
const WIPE_DEGREES = 360;
export const ICON_COLS = W.iconCols;

// Degrees clockwise from 12 o'clock; rows count `cellAspect` times as far as columns.
export const cellAngle = (row, col, cy, cx) => {
  const dx = col - cx;
  const dy = (cy - row) * W.cellAspect;
  if (dx === 0 && dy === 0) return 0;
  const deg = (Math.atan2(dx, dy) * 180) / Math.PI;
  return deg < 0 ? deg + 360 : deg;
};

// The [c0, c1) stretches of one row a hand `deg` into its turn has passed: 0, 1 or 2 of them.
export const sweptSpansAt = (row, cols, cy, cx, deg) => {
  if (deg <= 0 || cols === 0) return [];
  if (deg >= WIPE_DEGREES) return [[0, cols]];
  const out = [];
  let start = -1;
  for (let c = 0; c < cols; c++) {
    const passed = cellAngle(row, c + 1, cy, cx) < deg;
    if (passed && start < 0) start = c;
    else if (!passed && start >= 0) { out.push([start, c]); start = -1; }
  }
  if (start >= 0) out.push([start, cols]);
  return out;
};

// How far round the hand has come at seam column `x` of `reach`: one turn, `iconSpeed` times as fast.
export const iconDegrees = (x, reach) => {
  if (reach === 0 || x >= reach) return WIPE_DEGREES;
  return Math.min(WIPE_DEGREES, (WIPE_DEGREES * x * W.iconSpeed) / reach);
};

// Which columns of header row `row` (1-based, of `headerRows`) wear the new accent at seam `x`:
// inside the icon the clock decides, past it the seam.
export const iconSpans = (row, headerRows, cols, deg, x) => {
  const w = Math.min(ICON_COLS, cols);
  const spans = sweptSpansAt(row, w, (headerRows + 1) / 2, (w + 1) / 2, deg);
  if (x > w) spans.push([w, Math.min(x, cols)]);
  return spans;
};
