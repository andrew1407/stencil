// Zoom − / + dragged off the toolbar: the zoom follows the pointer's distance d from the button,
// z0·e^(∓k·d) about the viewport centre, z0 the zoom before the press; a release over the button
// restores z0. Fit dragged lights − and + as targets: over + it steps in at the hold rate, over −
// out; released over fit it returns to where the drag began. Desktop twin:
// desktop/src/app/drag/toolbarDragsZoom.cpp over zoomFollow.hpp.
import { wireIconDrag, markDropTarget } from './iconDrag.js';
import { holdStep, HOLD_REPEAT_MS } from '../bindings/viewport/holdZoom.js';

// 300 px of drag spans ×8 (zoom-in) or ÷8 (zoom-out): e^(k·300) = 8.
export const ZOOM_DRAG_K = Math.log(8) / 300;

export const zoomAtDistance = (z0, d, sign, clamp = (s) => s, k = ZOOM_DRAG_K) =>
  clamp(z0 * Math.exp(sign * k * d));

// `hold` is the button's setupHoldZoom handle: its repeat stops, and the step its press took is undone.
export const zoomDragHooks = ({ btn, sign, zp, hold = null }) => {
  let anchor = null;
  let centre = null;
  const show = (scale) => zp.restoreAnchor({ ...anchor, fit: false, scale });
  return {
    start: () => {
      hold?.stop();
      anchor = hold?.pressAnchor() ?? zp.viewAnchor();
      if (!anchor) return false;
      const r = btn.getBoundingClientRect();
      centre = { x: r.left + r.width / 2, y: r.top + r.height / 2 };
      show(anchor.scale);
      return true;
    },
    move: (p) => show(zoomAtDistance(anchor.scale, Math.hypot(p.x - centre.x, p.y - centre.y), sign,
                                     (s) => zp.clampScale(s))),
    drop: () => { anchor = null; },
    cancel: () => {
      if (anchor) zp.restoreAnchor(anchor);
      anchor = null;
    },
  };
};

// Over + or −, one step at once and then every `repeatMs` until the pointer leaves it.
export const createFitStepper = ({ step, repeatMs = HOLD_REPEAT_MS,
                                   setTimer = setInterval, clearTimer = clearInterval }) => {
  let sign = 0;
  let timer = null;
  const over = (next) => {
    if (next === sign) return;
    if (timer !== null) clearTimer(timer);
    timer = null;
    sign = next;
    if (!sign) return;
    step(sign);
    timer = setTimer(() => step(sign), repeatMs);
  };
  return { over, stop: () => over(0), get sign() { return sign; } };
};

const inside = (el, t) => !!el && !!t && (el === t || !!el.contains?.(t));

export const fitDragHooks = ({ zp, zoomIn, zoomOut,
                               stepper = createFitStepper({ step: (s) => holdStep(zp, s) }) }) => {
  let anchor = null;
  const light = (on, sign = 0) => {
    markDropTarget(zoomIn, on, sign > 0);
    markDropTarget(zoomOut, on, sign < 0);
  };
  const end = () => { stepper.stop(); light(false); };
  return {
    start: () => {
      anchor = zp.viewAnchor();
      if (!anchor) return false;
      light(true);
      return true;
    },
    move: (p) => {
      const sign = inside(zoomIn, p.target) ? 1 : inside(zoomOut, p.target) ? -1 : 0;
      light(true, sign);
      stepper.over(sign);
    },
    drop: end,
    cancel: () => {
      end();
      zp.restoreAnchor(anchor);
    },
  };
};

export const wireZoomDrag = (btn, sign, zp, hold) =>
  (btn ? wireIconDrag(btn, zoomDragHooks({ btn, sign, zp, hold })) : null);
export const wireFitDrag = (btn, zp, { zoomIn, zoomOut }) =>
  (btn ? wireIconDrag(btn, fitDragHooks({ zp, zoomIn, zoomOut })) : null);
