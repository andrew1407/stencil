// The genie warp: a window's picture cut into bands that bend into a funnel toward its icon and are
// sucked into it. A port of browser/js/ui/motion/genie/warp.js, pinned by tests/portParity.test.js.

// The near edge leads and the far edge trails it by LAG of the flight, so the window stretches as it
// pours; over the first PINCH_END the near bands narrow by BEND of the way to the icon's width.
export const GENIE_LAG = 0.55;
export const GENIE_PINCH_END = 0.4;
export const GENIE_BEND = 0.85;
// Picture px per band at most, and the band count's floor and ceiling.
const STRIP_PX = 3;
const MIN_STRIPS = 24;
const MAX_STRIPS = 240;

// A target past this AREA (a GENIE_MAX_ICON_PX square) is no icon but a region (the canvas a closing
// window pours into): the window flows to a point of SINK_PX at its centre instead of spreading over
// it. An edge strip of any length (a selector's underside) stays as it is.
export const GENIE_MAX_ICON_PX = 96;
const SINK_PX = 24;

const clamp01 = (v) => Math.min(1, Math.max(0, v));
const smooth = (v) => { const x = clamp01(v); return x * x * (3 - 2 * x); };
const lerp = (a, b, t) => a + (b - a) * t;

// The rect the window flows into: `icon` itself, or a point at its centre when it is a region.
export function genieSink(icon) {
  if (icon.width * icon.height <= GENIE_MAX_ICON_PX * GENIE_MAX_ICON_PX) return icon;
  return { left: icon.left + icon.width / 2 - SINK_PX / 2, top: icon.top + icon.height / 2 - SINK_PX / 2,
           width: SINK_PX, height: SINK_PX };
}

// The edge of `box` the point lies beyond, the widest gap winning; null over the box
// (desktop support/genie/genieWarp.hpp sideOf).
export function genieSide(box, x, y) {
  const gaps = { down: y - (box.top + box.height), up: box.top - y, right: x - (box.left + box.width), left: box.left - x };
  let best = null;
  for (const [side, gap] of Object.entries(gaps)) if (gap > 0 && (best === null || gap > gaps[best])) best = side;
  return best;
}

// The strip of `r` that faces `box` (a selector's underside for a list below it), or `r` whole where they overlap.
export function genieFacing(r, box) {
  if (box.top >= r.top + r.height - 1) return { left: r.left, top: r.top + r.height - 2, width: r.width, height: 2 };
  if (box.top + box.height <= r.top + 1) return { left: r.left, top: r.top, width: r.width, height: 2 };
  return r;
}

// The edge the icon lies beyond, the widest gap winning; an icon over the window flows the way its
// centre lies from the window's.
export function genieFlowSide(win, icon) {
  const cx = icon.left + icon.width / 2;
  const cy = icon.top + icon.height / 2;
  const best = genieSide(win, cx, cy);
  if (best) return best;
  const dx = cx - (win.left + win.width / 2);
  const dy = cy - (win.top + win.height / 2);
  if (Math.abs(dy) >= Math.abs(dx)) return dy >= 0 ? 'down' : 'up';
  return dx >= 0 ? 'right' : 'left';
}

// A rect in canonical form: `a` runs toward the icon (a0 = the far edge), `c` beside it.
const canon = (r, side) => {
  const right = r.left + r.width, bottom = r.top + r.height;
  if (side === 'down') return { a0: r.top, a1: bottom, c0: r.left, c1: right };
  if (side === 'up') return { a0: -bottom, a1: -r.top, c0: r.left, c1: right };
  if (side === 'right') return { a0: r.left, a1: right, c0: r.top, c1: bottom };
  return { a0: -right, a1: -r.left, c0: r.top, c1: bottom };
};
const uncanon = (a0, a1, c0, c1, side) => {
  if (side === 'down') return { left: c0, top: a0, width: c1 - c0, height: a1 - a0 };
  if (side === 'up') return { left: c0, top: -a1, width: c1 - c0, height: a1 - a0 };
  if (side === 'right') return { left: a0, top: c0, width: a1 - a0, height: c1 - c0 };
  return { left: -a1, top: c0, width: a1 - a0, height: c1 - c0 };
};
// A band of the picture as fractions: `v` runs from the window's far edge to its near one.
const sourceBand = (v0, v1, side) => {
  if (side === 'down') return { x: 0, y: v0, w: 1, h: v1 - v0 };
  if (side === 'up') return { x: 0, y: 1 - v1, w: 1, h: v1 - v0 };
  if (side === 'right') return { x: v0, y: 0, w: v1 - v0, h: 1 };
  return { x: 1 - v1, y: 0, w: v1 - v0, h: 1 };
};

// How far picture row `v` (0 = far edge, 1 = near edge) has travelled at progress `p`.
export const genieTravel = (v, p) => smooth(p * (1 + GENIE_LAG) - GENIE_LAG * (1 - v));

// The bands at progress `p`: 0 = the window at rest, 1 = shrunk onto the icon. Each is
// `{ target: rect, source: fractions }`.
export function genieWarp(win, icon, side, p) {
  const w = canon(win, side), i = canon(icon, side);
  const len = w.a1 - w.a0;
  if (!(len > 0 && w.c1 > w.c0)) return [];
  const pinch = smooth(p / GENIE_PINCH_END);
  const n = Math.min(MAX_STRIPS, Math.max(MIN_STRIPS, Math.floor(len / STRIP_PX)));
  const along = (v) => lerp(w.a0 + v * len, i.a0 + v * (i.a1 - i.a0), genieTravel(v, p));
  const out = [];
  let a0 = along(0);
  for (let k = 0; k < n; k++) {
    const v0 = k / n, v1 = (k + 1) / n, vm = (v0 + v1) / 2;
    const a1 = along(v1);
    const f = Math.max(genieTravel(vm, p), pinch * GENIE_BEND * smooth(vm));
    out.push({ target: uncanon(a0, a1, lerp(w.c0, i.c0, f), lerp(w.c1, i.c1, f), side), source: sourceBand(v0, v1, side) });
    a0 = a1;
  }
  return out;
}

// The warp's silhouette at `p`: `rows` + 1 points down one side and back up the other, as client points.
// A fixed count, so one frame's mask interpolates into the next (the theme swap's Sliding reveal).
export function genieOutline(win, icon, side, p, rows = 24) {
  const w = canon(win, side), i = canon(icon, side);
  const len = w.a1 - w.a0;
  const pinch = smooth(p / GENIE_PINCH_END);
  const one = [], other = [];
  for (let k = 0; k <= rows; k++) {
    const v = k / rows;
    const a = lerp(w.a0 + v * len, i.a0 + v * (i.a1 - i.a0), genieTravel(v, p));
    const f = Math.max(genieTravel(v, p), pinch * GENIE_BEND * smooth(v));
    const r = uncanon(a, a, lerp(w.c0, i.c0, f), lerp(w.c1, i.c1, f), side);
    one.push({ x: r.left, y: r.top });
    other.push({ x: r.left + r.width, y: r.top + r.height });
  }
  return one.concat(other.reverse());
}

