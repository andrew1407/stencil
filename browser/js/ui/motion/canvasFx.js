import { dustEnabled } from '../motionPrefs.js';
import { runDust } from './canvasDustDraw.js';
import { GHOST_MS } from './canvasDustGrid.js';
import { makeDustStage } from './canvasDustStage.js';
import { arriveFrom, flashLanding } from './enterLeave.js';
// ── Canvas dust, the other way (ghostIn) ────────────────────────────────────
// The arrival is the clear PLAYED BACKWARDS: same grid, noise and fall, sweep
// reversed, so the picture builds bottom-to-top. It deliberately does NOT stream in
// from the drop point: the stage canvas is only as big as the image, and a mote pulled
// toward the cursor spends the whole flight outside it, clipped away.
// Returns true when the dust is actually playing: the caller only hides the real
// canvas if it is, or a bail-out (reduced motion, tiny canvas, no rAF) would leave a
// blank editor.
// Is anything actually drawn here? Sampled, not exhaustive — a stride wide enough to be
// cheap on a big canvas and fine enough that no real image reads as empty.
export function hasPixels(ctx, w, h, stride = 41) {
  if (!ctx?.getImageData || w < 1 || h < 1) return false;
  const d = ctx.getImageData(0, 0, w, h).data;
  for (let p = 3; p < d.length; p += 4 * stride) if (d[p] > 0) return true;
  return false;
}

export function ghostIn(canvas, { ms = GHOST_MS } = {}) {
  if (typeof document === 'undefined' || !canvas?.width || !canvas.height) return false;
  if (!dustEnabled()) return false;
  if (typeof requestAnimationFrame === 'undefined' || !canvas.parentElement) return false;
  try {
    const st = makeDustStage(canvas);
    if (!st) return false;
    // The snapshot has to HAVE something: taken before the load has painted, the dust
    // is invisible motes in front of a canvas the caller is holding hidden. Saying no
    // makes the caller fall back to the plain flight instead of animating nothing.
    if (!hasPixels(st.snapCtx, st.snap.width, st.snap.height)) { st.finish(); return false; }
    // The fall inverted: ghostOut carries a grain DOWN and away, so the arrival starts
    // it down there and lifts it home (dustParts, gather).
    runDust(st, ms, true);
    return true;
  } catch {
    return false;   // decoration only — the image is already loaded either way
  }
}

// ── The one place an image's ARRIVAL is played ──────────────────────────────
// Every route that puts a picture on the canvas ends here: a fresh file load AND a
// reopened project. It used to live inline in the loader only, so opening a saved
// project painted the image with no motion at all.
// Dust gather when ghostIn can run (the canvas waits behind it under
// ASSEMBLING_CLASS), the drop-point flight when it can't. Returns which one played,
// so the wiring is testable without a DOM.
export const ASSEMBLING_CLASS = 'canvas-assembling';
export const CLEARING_CLASS = 'canvas-clearing';

export function playCanvasArrival(canvas, { from = null, viewport, container, ghost = ghostIn } = {}) {
  const doc = typeof document !== 'undefined' ? document : null;
  // Both looked up by ID: the loader used to reach for the viewport by CLASS, and the
  // fullscreen layer clones the shell — two elements answering to one selector.
  const vp = viewport !== undefined ? viewport : doc?.getElementById('canvas-viewport') || null;
  const box = container !== undefined ? container : doc?.getElementById('canvas-container') || null;
  if (ghost(canvas)) {
    if (vp) flashLanding(vp, ASSEMBLING_CLASS, GHOST_MS);
    return 'dust';
  }
  // ghostIn declined (reduced motion, a canvas too small, nothing painted yet) — the
  // plain landing still runs, and is itself inert under reduced motion.
  arriveFrom(box, from);
  return 'flight';
}
