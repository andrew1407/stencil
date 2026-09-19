import { motionReduced } from '../motionPrefs.js';

// The crop rect's flight between two shapes (an Album/Portrait flip), on the dialog's
// height-ease clock (easeBoxHeight.js; desktop twin: cropDialogParts.hpp CROP_TWEEN_MS).
export const RECT_TWEEN_MS = 380;

export const easeOutCubic = (t) => 1 - Math.pow(1 - t, 3);   // QEasingCurve::OutCubic

const lerp = (a, b, k) => a + (b - a) * k;

// Hands `onFrame` a rect per frame, `from` at once and `to` itself last; returns a cancel,
// after which no frame follows. No `from`, no rAF or reduced motion: `to` now.
export const tweenRect = (from, to, onFrame, ms = RECT_TWEEN_MS) => {
  if (!from || motionReduced() || typeof requestAnimationFrame !== 'function') {
    onFrame(to);
    return () => {};
  }
  const t0 = performance.now();
  let raf = 0;
  const step = (now) => {
    const t = Math.min(1, Math.max(0, (now - t0) / ms));
    if (t >= 1) { onFrame(to); return; }
    const k = easeOutCubic(t);
    onFrame({ x: lerp(from.x, to.x, k), y: lerp(from.y, to.y, k),
              width: lerp(from.width, to.width, k), height: lerp(from.height, to.height, k) });
    raf = requestAnimationFrame(step);
  };
  onFrame(from);
  raf = requestAnimationFrame(step);
  return () => cancelAnimationFrame(raf);
};
