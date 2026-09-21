import { dustEnabled } from '../../motionPrefs.js';
import { runDust } from '../dust/canvasDustDraw.js';
import { GHOST_MS } from '../dust/canvasDustGrid.js';
import { makeDustStage } from '../dust/canvasDustStage.js';
import { arriveFrom, flashLanding } from '../enterLeave.js';
// The arrival is the clear played backwards (sweep reversed, bottom-to-top); it does not
// stream from the drop point because the stage is only as big as the image. Returns true
// only when the dust is playing — the caller hides the real canvas only then.
// Sampled, not exhaustive: cheap on a big canvas, fine enough that no image reads as empty.
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
// Taken before the load has painted, the snapshot is invisible motes over a hidden canvas.
    if (!hasPixels(st.snapCtx, st.snap.width, st.snap.height)) { st.finish(); return false; }
    runDust(st, ms, true);
    return true;
  } catch {
    return false;   // decoration only — the image is already loaded either way
  }
}

// Every route that puts a picture on the canvas ends here: the dust gather when ghostIn
// can run, the drop-point flight otherwise. Returns which one played.
export const ASSEMBLING_CLASS = 'canvas-assembling';
export const CLEARING_CLASS = 'canvas-clearing';

export function playCanvasArrival(canvas, { from = null, viewport, container, ghost = ghostIn } = {}) {
  const doc = typeof document !== 'undefined' ? document : null;
// By ID: the fullscreen layer clones the shell, so a class selector answers twice.
  const vp = viewport !== undefined ? viewport : doc?.getElementById('canvas-viewport') || null;
  const box = container !== undefined ? container : doc?.getElementById('canvas-container') || null;
  if (ghost(canvas)) {
    if (vp) flashLanding(vp, ASSEMBLING_CLASS, GHOST_MS);
    return 'dust';
  }
  arriveFrom(box, from);
  return 'flight';
}
