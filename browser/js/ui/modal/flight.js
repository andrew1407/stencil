import { motionMs, motionReduced } from '../motion/motionPrefs.js';
import { originOf } from '../motion/surface/themeSwapPlay.js';
import { SURFACE_OUT_MS } from '../motion/surface/motion.js';
import { surfaceIn, surfaceOut, settleSurface } from '../motion/surface/surfaces.js';
import { applyGenieVars, clearGenieVars } from '../motion/genie/vars.js';
import { createWindowGenie, genieEnabled } from '../motion/genie/window.js';
import { GENIE_CLOSE_MS } from '../motion/genie/flight.js';

// A window forms from motes streaming out of the control that opened it and pours back
// into it. Shared with the confirm dialog, which has no opener button. The `--modal-*` vars
// (motion/genie/vars.js) drive modalFromIcon/modalToIcon wherever the dust and the genie decline.
export const MODAL_CLOSE_MS = SURFACE_OUT_MS;
// A control's client rect only while it is really on screen: folded away with the tool rows it
// keeps a rect (visibility, opacity, clipping), and a flight aimed there came out of nothing.
// In fullscreen the shown control is the strip's clone, which carries the hidden one's id.
const shownTwin = (el) => {
  if (originOf(el)) return el;
  if (!el?.id || typeof document?.querySelectorAll !== 'function') return null;
  for (const t of document.querySelectorAll(`[id="${el.id}"]`)) if (t !== el && originOf(t)) return t;
  return null;
};
export const shownRect = (el) => shownTwin(el)?.getBoundingClientRect() ?? null;
export const createModalFlight = (overlay, boxOf) => {
  let closeTimer = null;
  let originPoint = null;   // the origin centre, in client coordinates
  let restRect = null;      // the box at rest and the opener, for the genie (client rects)
  let iconRect = null;
  const genie = createWindowGenie(boxOf);
  const reducedMotion = () => motionReduced();
  // The genie's canvas sits one level over its own window.
  const zAbove = () => (overlay.style?.zIndex ? Number(overlay.style.zIndex) + 1 : undefined);

  // `anchor` is a client rect or null; a hidden or scrolled-away opener falls from above.
  const setOrigin = (anchor) => {
    const box = boxOf();
    if (!box) return false;
    // `both` fill means the box already wears the from-state transform; measure with it off.
    overlay.classList.add('modal-measuring');
    const b = box.getBoundingClientRect();
    overlay.classList.remove('modal-measuring');
    if (!b.width || !b.height) return false;
    const a = anchor;
    const onScreen = !!a && a.width > 0 && a.height > 0 && a.bottom > 0 && a.top < window.innerHeight;
    const cx = onScreen ? a.left + a.width / 2 : b.left + b.width / 2;
    const cy = onScreen ? a.top + a.height / 2 : -Math.max(48, b.height * 0.3);
    originPoint = { x: cx, y: cy };
    restRect = b;
    // No opener: the window drips up out of a strip at the origin above the page.
    iconRect = onScreen ? a : { left: cx - b.width * 0.15, top: cy - 12, width: b.width * 0.3, height: 24 };
    applyGenieVars(box, b, onScreen ? a : null);
    return true;
  };
  const clearOriginVars = () => clearGenieVars(boxOf());
  const finishClose = () => {
    if (closeTimer) { clearTimeout(closeTimer); closeTimer = null; }
    genie.settle();
    overlay.classList.remove('modal-closing', 'modal-popover');
    const box = boxOf();
    if (box) { box.style.left = ''; box.style.top = ''; }
    clearOriginVars();
  };
  // Both flights start here, so a superseding open/close drops the one in the air.
  const playDust = (enter) => {
    const box = boxOf();
    if (!box) return;
    if (reducedMotion()) { settleSurface(box); return; }
    // A side-by-side window's cloud flies at the window's own level, under any window above it.
    const z = overlay.style?.zIndex;
    if (enter && genieEnabled() && restRect) { genie.open(restRect, iconRect, zAbove()); return; }
    // The hold a Sliding open left (genie-hold: animation none) would still the CSS flight a mode picked since plays.
    genie.settle();
    (enter ? surfaceIn : surfaceOut)(box, originPoint, z ? { zIndex: z } : {});
  };
  // Measure while the window is still up, hand over to the cloud, wear `modal-closing`.
  const playClosing = () => {
    overlay.classList.add('modal-closing');
    if (closeTimer) clearTimeout(closeTimer);
    if (genieEnabled() && restRect) {
      genie.close(restRect, iconRect, zAbove(), finishClose, motionMs(MODAL_CLOSE_MS));
      // A flight that never lands (a picture that never decodes) still ends the close.
      closeTimer = setTimeout(() => { closeTimer = null; finishClose(); }, motionMs(GENIE_CLOSE_MS + MODAL_CLOSE_MS) + 1000);
      return;
    }
    playDust(false);
    closeTimer = setTimeout(() => { closeTimer = null; finishClose(); }, motionMs(MODAL_CLOSE_MS));
  };
  return { reducedMotion, setOrigin, finishClose, playDust, playClosing,
           settle: () => settleSurface(boxOf()) };
};
