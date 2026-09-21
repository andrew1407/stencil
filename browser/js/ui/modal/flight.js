import { surfaceIn, surfaceOut, settleSurface, SURFACE_OUT_MS, motionReduced } from '../motion.js';

// A window forms from motes streaming out of the control that opened it and pours back
// into it. Shared with the confirm dialog, which has no opener button. The `--modal-*` vars
// stay: modalFromIcon/modalToIcon play wherever the dust declines (a stub, reduced motion).
export const MODAL_CLOSE_MS = SURFACE_OUT_MS;
export const createModalFlight = (overlay, boxOf) => {
  let closeTimer = null;
  let originPoint = null;   // the origin centre, in client coordinates
  const reducedMotion = () => motionReduced();

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
    box.style.setProperty('--modal-dx', `${Math.round(cx - (b.left + b.width / 2))}px`);
    box.style.setProperty('--modal-dy', `${Math.round(cy - (b.top + b.height / 2))}px`);
    // Floored so a big window doesn't animate from a sub-pixel speck.
    box.style.setProperty('--modal-sx', String(onScreen ? Math.max(a.width / b.width, 0.05) : 0.4));
    box.style.setProperty('--modal-sy', String(onScreen ? Math.max(a.height / b.height, 0.05) : 0.4));
    return true;
  };
  const clearOriginVars = () => {
    const box = boxOf();
    if (!box) return;
    for (const v of ['--modal-dx', '--modal-dy', '--modal-sx', '--modal-sy']) box.style.removeProperty(v);
  };
  const finishClose = () => {
    if (closeTimer) { clearTimeout(closeTimer); closeTimer = null; }
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
    (enter ? surfaceIn : surfaceOut)(box, originPoint);
  };
  // Measure while the window is still up, hand over to the cloud, wear `modal-closing`.
  const playClosing = () => {
    overlay.classList.add('modal-closing');
    playDust(false);
    if (closeTimer) clearTimeout(closeTimer);
    closeTimer = setTimeout(() => { closeTimer = null; finishClose(); }, MODAL_CLOSE_MS);
  };
  return { reducedMotion, setOrigin, finishClose, playDust, playClosing,
           settle: () => settleSurface(boxOf()) };
};
