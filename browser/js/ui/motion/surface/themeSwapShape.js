// The theme swap's reveal per interface animation: Transition wipes the new palette in from the left edge;
// Sliding pours a theme change out of its control and an accent change down from the top, as a popup does, its
// mask the genie's own silhouette (genie/warp.js).
// Twins: extension lib/dust/swapGenie.js, desktop support/dust/ThemeSwapOverlayPaint.cpp holeAt.
import { genieOutline } from '../genie/warp.js';
import { GENIE_OPEN_MS } from '../genie/flight.js';

export const WIPE_CLIP_FROM = 'inset(0 100% 0 0)';
export const WIPE_CLIP_TO = 'inset(0 0 0 0)';
export const SWAP_GENIE_MS = GENIE_OPEN_MS;
export const SWAP_GENIE_KEYFRAMES = 'themeSwapGenie';
// Keyframes evenly spaced in time along the flight's out-sine; the silhouette's points down each side.
export const SWAP_GENIE_STEPS = 16;
export const SWAP_GENIE_ROWS = 24;
// The control as the icon a theme change pours out of (px); an accent change pours down out of the whole top edge.
const SWAP_SOURCE_PX = 24;

// The box edge nearest the control: the way a theme change pours (a toolbar button: out of the top).
export function swapFlowSide(x, y, w, h) {
  const gaps = [['up', y], ['down', h - y], ['left', x], ['right', w - x]];
  return gaps.reduce((a, b) => (b[1] < a[1] ? b : a))[0];
}

// The silhouette at warp progress `p` (1 = in its source, 0 = the whole box), as a clip-path: a theme change
// pours out of the control; `fromTop` (an accent change) comes down from the whole top edge to the bottom.
export function swapGeniePolygon(x, y, w, h, p, fromTop = false) {
  const half = SWAP_SOURCE_PX / 2;
  const source = fromTop ? { left: 0, top: 0, width: w, height: 1 }
    : { left: x - half, top: y - half, width: SWAP_SOURCE_PX, height: SWAP_SOURCE_PX };
  const side = fromTop ? 'up' : swapFlowSide(x, y, w, h);
  const pts = genieOutline({ left: 0, top: 0, width: w, height: h }, source, side, p, SWAP_GENIE_ROWS);
  const pc = (v, of) => Math.round((100000 * v) / of) / 1000;
  return `polygon(${pts.map((q) => `${pc(q.x, w)}% ${pc(q.y, h)}%`).join(', ')})`;
}

// The warp progress at time `t` (0..1): a theme change on the window flight's out-sine; an accent change's
// sweep down on an in-out sine, so it is seen travelling the whole height rather than landing at once.
export const swapProgress = (t, fromTop = false) => (fromTop ? (1 + Math.cos(Math.PI * t)) / 2 : 1 - Math.sin((t * Math.PI) / 2));

// The @keyframes the swap's new snapshot plays on a linear clock (animations/themeSwap.css html.theme-swap-genie).
export function swapGenieKeyframes(x, y, w, h, fromTop = false) {
  if (!(w > 0 && h > 0)) return '';
  const frames = [];
  for (let k = 0; k <= SWAP_GENIE_STEPS; k++) {
    const t = k / SWAP_GENIE_STEPS;
    frames.push(`${Math.round(t * 1000) / 10}% { clip-path: ${swapGeniePolygon(x, y, w, h, swapProgress(t, fromTop), fromTop)}; }`);
  }
  return `@keyframes ${SWAP_GENIE_KEYFRAMES} { ${frames.join(' ')} }`;
}
