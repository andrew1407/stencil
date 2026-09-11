import { TUNE } from './tune.js';
// ── Ghosting a canvas out ───────────────────────────────────────────────────
// Clearing the canvas is synchronous, so the pixels are copied into a throwaway canvas
// laid over the real one and THAT is animated away. Call immediately BEFORE the clear;
// a failed ghost must never block the clear itself.
// Cut to 730 once for briskness and half again as long since, which lands it back near
// the 1100ms it started at: the picture is worth watching arrive and leave.
export const GHOST_MS = TUNE.GHOST_MS;
// Dust motes are sized on SCREEN, not as a share of the image: a fixed grid over a
// big canvas gives big rectangles, which is what stopped it reading as dust.
export const DUST_CELL_PX = TUNE.DUST_CELL_PX;
// Halving the cell quadruples the count, so the ceiling has to rise with it — but it
// still exists: past this, a frame costs more than the effect is worth and the grid is
// thinned back instead.
export const DUST_MAX_PARTICLES = TUNE.DUST_MAX_PARTICLES;

// The grid the ghost dust flies on: aim for `cellPx` per mote ON SCREEN (a water / fire
// ghost grids coarser), thinned evenly once the particle ceiling bites. Pure.
export const dustGrid = (w, h, cellPx = DUST_CELL_PX) => {
  let cols = Math.max(1, Math.round(w / cellPx));
  let rows = Math.max(1, Math.round(h / cellPx));
  while (cols * rows > DUST_MAX_PARTICLES) { cols = Math.ceil(cols / 1.1); rows = Math.ceil(rows / 1.1); }
  return { cols, rows };
};

// Clip the canvas box to the frame it is seen through. Zoom scales the canvas's CSS box
// while the viewport stays put, so gridding the WHOLE box let the particle ceiling thin
// a zoomed-in canvas back into big flakes — mote size followed the zoom level. Only the
// slice inside the frame is visible (the viewport scrolls the rest away), so only it
// plays, and the grid stays viewport-bounded at any zoom. Pure — unit-tested.
export const dustVisibleBox = (r, frame) => {
  const left = Math.max(r.left, frame.left), top = Math.max(r.top, frame.top);
  return { left, top,
           width: Math.min(r.left + r.width, frame.left + frame.width) - left,
           height: Math.min(r.top + r.height, frame.top + frame.height) - top };
};

// The canvas disintegrates as DUST, downward — a particle system on ONE canvas, not
// the DOM tiles a row uses: thousands of elements would not survive a frame. Each mote
// is a round grain in the colour of the pixels it came from (sampled once, dustParts);
// while a cell is still at home it is the picture itself — the visible slice is blitted
// whole each frame and only the departed cells are cleared out of it — so the front
// reads as the image grinding into grains of its own colour, not a dot screen popping
// over it. Landing is the same run backwards: a cell that has settled is the picture.
// When a mote's clock starts, as a fraction of the flight: OUT runs TOP-DOWN; IN is
// the same sweep REVERSED, so the mote that left first is the last one home. Pure.
export const dustDelay = (cy, rows, n, reverse = false) => {
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  return (reverse ? 1 - progress : progress) * 0.55 + n * 0.12;
};

// Ease-out for the arrival: a mote covers most of the distance early and settles into
// place, rather than crawling the last few pixels. Pure.
export const dustEase = (k) => 1 - (1 - k) ** 3;

// Anchor to the VIEWPORT frame, not canvas.parentElement (.canvas-container): the
// container sizes itself to the canvas and re-centers via `margin: auto`, so zeroing
// canvas.width/height right after (the clear) collapses it to 0×0 and strands an
// absolutely-positioned child at its new, recentred spot. The viewport never shrinks.
const canvasDustHost = (canvas) => canvas.closest?.('.canvas-viewport') || canvas.parentElement;

// Measure where the dust plays: the visible slice of the canvas, plus the stage's
// offsets inside the host. clientLeft/Top step over the host's border, and the scroll
// offsets are added because an absolutely-positioned stage scrolls WITH the content —
// without them a panned, zoomed-in canvas got its cloud a scroll-offset away.
export const dustField = (canvas) => {
  const r = canvas.getBoundingClientRect();
  const host = canvasDustHost(canvas);
  const hr = host.getBoundingClientRect();
  const frame = { left: hr.left + (host.clientLeft || 0), top: hr.top + (host.clientTop || 0),
                  width: host.clientWidth || hr.width, height: host.clientHeight || hr.height };
  const vis = dustVisibleBox(r, frame);
  if (vis.width < 8 || vis.height < 8) return null;
  return { r, host, vis,
           left: vis.left - frame.left + (host.scrollLeft || 0),
           top: vis.top - frame.top + (host.scrollTop || 0) };
};

// Keep a flying stage over the frame: it is absolutely positioned inside a SCROLLING
// host, so left alone it rides away with the content — a project restore jumps to its
// saved scroll right after the arrival goes up, and at high zoom that carried the whole
// cloud off-screen ("no animation at all"). Returns an unpin to call on removal.
