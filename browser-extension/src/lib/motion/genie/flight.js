// The genie flight: a window's picture warped between its box and its icon on a canvas over the page.
// A port of browser/js/ui/motion/genie/flight.js, pinned by tests/portParity.test.js.
import { genieFlowSide, genieSink, genieWarp } from './warp.js';

// The tuned lengths (ms) at the default speed: the open is the unhurried half.
export const GENIE_OPEN_MS = 560;
export const GENIE_CLOSE_MS = 480;
export const GENIE_CANVAS_CLASS = 'genie-flight';
// Each band overlaps its neighbours by this much (CSS px), so no seam shows at a fractional scale.
const SEAM_PX = 0.25;

// Gentle at both ends: each band already eases on its own (warp.js genieTravel).
const easeInOutSine = (t) => -(Math.cos(Math.PI * t) - 1) / 2;
const easeOutSine = (t) => Math.sin((t * Math.PI) / 2);
// Room past the flight's own span (CSS px), so the seam overlap is never clipped.
const BOUNDS_PAD_PX = 4;

// Paints the warp at progress `p` onto `ctx` (CSS px; the context carries the device scale).
export function paintGenie(ctx, shot, win, icon, side, p) {
  ctx.save();
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
  ctx.restore();
  const sw = shot.width, sh = shot.height;
  for (const { target: t, source: s } of genieWarp(win, icon, side, p)) {
    if (!(t.width > 0 && t.height > 0)) continue;
    ctx.drawImage(shot, s.x * sw, s.y * sh, s.w * sw, s.h * sh,
      t.left - SEAM_PX, t.top - SEAM_PX, t.width + 2 * SEAM_PX, t.height + 2 * SEAM_PX);
  }
}

// `win` and `icon` are client rects; `opening` plays it out of the icon. `done` runs once, as it
// lands or is cancelled. Returns `cancel()`.
export function playGenie(shot, win, target, { opening, ms, zIndex = 100005, done, clip = null } = {}) {
  const icon = genieSink(target);
  const doc = globalThis.document;
  const canvas = doc.createElement('canvas');
  const dpr = globalThis.devicePixelRatio || 1;
  // Only as big as the flight (inside the viewport): a page-sized canvas cleared and composited on
  // every frame is what made a large window's flight stutter.
  const vw = globalThis.innerWidth, vh = globalThis.innerHeight;
  const left = Math.max(0, Math.floor(Math.min(win.left, icon.left) - BOUNDS_PAD_PX));
  const top = Math.max(0, Math.floor(Math.min(win.top, icon.top) - BOUNDS_PAD_PX));
  const right = Math.min(vw, Math.ceil(Math.max(win.left + win.width, icon.left + icon.width) + BOUNDS_PAD_PX));
  const bottom = Math.min(vh, Math.ceil(Math.max(win.top + win.height, icon.top + icon.height) + BOUNDS_PAD_PX));
  const w = Math.max(1, right - left), h = Math.max(1, bottom - top);
  canvas.className = GENIE_CANVAS_CLASS;
  canvas.setAttribute('aria-hidden', 'true');
  canvas.width = Math.round(w * dpr);
  canvas.height = Math.round(h * dpr);
  Object.assign(canvas.style, {
    position: 'fixed', left: `${left}px`, top: `${top}px`, width: `${w}px`, height: `${h}px`,
    pointerEvents: 'none', zIndex: String(zIndex),
  });
  // `clip` (a client rect): the flight never paints outside it — a chat entry stays inside its transcript.
  if (clip) {
    const cut = (n) => `${Math.max(0, Math.round(n))}px`;
    canvas.style.clipPath = `inset(${cut(clip.top - top)} ${cut(right - clip.left - clip.width)} ${cut(bottom - clip.top - clip.height)} ${cut(clip.left - left)})`;
  }
  doc.body.appendChild(canvas);
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, -left * dpr, -top * dpr);
  ctx.imageSmoothingQuality = 'high';
  const side = genieFlowSide(win, icon);
  const progressAt = (t) => (opening ? 1 - easeOutSine(t) : easeInOutSine(t));
  paintGenie(ctx, shot, win, icon, side, progressAt(0));

  let frame = 0;
  let over = false;
  const finish = () => {
    if (over) return;
    over = true;
    globalThis.cancelAnimationFrame?.(frame);
    canvas.remove();
    done?.();
  };
  const t0 = globalThis.performance.now();
  const step = (now) => {
    const t = Math.min(1, Math.max(0, (now - t0) / Math.max(1, ms)));
    paintGenie(ctx, shot, win, icon, side, progressAt(t));
    if (t >= 1) finish();
    else frame = globalThis.requestAnimationFrame(step);
  };
  frame = globalThis.requestAnimationFrame(step);
  return finish;
}
