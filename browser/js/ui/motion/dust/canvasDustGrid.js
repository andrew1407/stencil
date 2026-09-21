import { TUNE } from '../tune.js';
// Clearing the canvas is synchronous, so the pixels are copied into a throwaway canvas over
// the real one and THAT is animated away. Call immediately BEFORE the clear.
export const GHOST_MS = TUNE.GHOST_MS;
// Motes are sized on SCREEN, not as a share of the image.
export const DUST_CELL_PX = TUNE.DUST_CELL_PX;
// Past this many particles a frame costs more than the effect is worth; the grid thins.
export const DUST_MAX_PARTICLES = TUNE.DUST_MAX_PARTICLES;

export const dustGrid = (w, h, cellPx = DUST_CELL_PX) => {
  let cols = Math.max(1, Math.round(w / cellPx));
  let rows = Math.max(1, Math.round(h / cellPx));
  while (cols * rows > DUST_MAX_PARTICLES) { cols = Math.ceil(cols / 1.1); rows = Math.ceil(rows / 1.1); }
  return { cols, rows };
};

// Only the slice inside the frame plays: zoom scales the canvas box while the viewport
// stays put, so gridding the whole box let the ceiling thin a zoomed canvas into flakes.
export const dustVisibleBox = (r, frame) => {
  const left = Math.max(r.left, frame.left), top = Math.max(r.top, frame.top);
  return { left, top,
           width: Math.min(r.left + r.width, frame.left + frame.width) - left,
           height: Math.min(r.top + r.height, frame.top + frame.height) - top };
};

// One canvas of grains in the colour of the pixels they came from. OUT runs top-down; IN is
// the same sweep reversed, so the first mote out is the last one home.
export const dustDelay = (cy, rows, n, reverse = false) => {
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  return (reverse ? 1 - progress : progress) * 0.55 + n * 0.12;
};

export const dustEase = (k) => 1 - (1 - k) ** 3;

// Anchor to the viewport frame, not .canvas-container: the container collapses to 0×0
// when canvas.width/height are zeroed by the clear.
const canvasDustHost = (canvas) => canvas.closest?.('.canvas-viewport') || canvas.parentElement;

// The visible slice plus the stage's offsets inside the host: clientLeft/Top step over the
// border, and the scroll offsets count because an absolute stage scrolls WITH the content.
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

// A stage absolutely positioned inside a scrolling host rides away with the content (a
// project restore jumps to its saved scroll). Returns an unpin to call on removal.
