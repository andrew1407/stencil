import { surfaceIn, surfaceOut, settleSurface, SURFACE_OUT_MS, motionReduced } from './motion.js';

// ── Grow-from-the-icon motion, on its own (js/ui/motion.js surfaceIn/surfaceOut) ──
// A window forms from motes streaming out of the control that opened it, and comes
// apart into motes pouring back into it. Extracted from the shell below because the
// CONFIRM dialog needs the same flight and has no opener button to hang one off: it is
// raised by whatever the user just did, so its origin is that gesture's own point.
// The `--modal-*` vars stay — they are the flight modalFromIcon/modalToIcon plays
// wherever the dust declines (an unmeasurable box, a stub, reduced motion).
export const MODAL_CLOSE_MS = SURFACE_OUT_MS;   // the dust's own clock (config/motion.json)
export const createModalFlight = (overlay, boxOf) => {
  let closeTimer = null;
  let originPoint = null;   // the origin centre, in client coordinates
  // The shared gate (ui/motionPrefs.js): the OS preference OR the user's 'none' mode.
  const reducedMotion = () => motionReduced();

  // `anchor` is a client rect (an icon's, or a small box around a click) or null — a
  // hidden opener measures 0x0 and a scrolled-away one sits outside the viewport, and
  // both fall from above instead.
  const setOrigin = (anchor) => {
    const box = boxOf();
    if (!box) return false;
    // Measure with the animation suppressed: `both` fill means the box already wears the
    // from-state transform, so an unguarded rect feeds our own offsets back in.
    overlay.classList.add('modal-measuring');
    const b = box.getBoundingClientRect();
    overlay.classList.remove('modal-measuring');
    if (!b.width || !b.height) return false;
    const a = anchor;
    const onScreen = !!a && a.width > 0 && a.height > 0 && a.bottom > 0 && a.top < window.innerHeight;
    const cx = onScreen ? a.left + a.width / 2 : b.left + b.width / 2;
    const cy = onScreen ? a.top + a.height / 2 : -Math.max(48, b.height * 0.3);
    originPoint = { x: cx, y: cy };   // the dust streams out of / pours into this point
    box.style.setProperty('--modal-dx', `${Math.round(cx - (b.left + b.width / 2))}px`);
    box.style.setProperty('--modal-dy', `${Math.round(cy - (b.top + b.height / 2))}px`);
    // Floor the ratio so a big window doesn't animate from a sub-pixel speck.
    box.style.setProperty('--modal-sx', String(onScreen ? Math.max(a.width / b.width, 0.05) : 0.4));
    box.style.setProperty('--modal-sy', String(onScreen ? Math.max(a.height / b.height, 0.05) : 0.4));
    return true;
  };
  const clearOriginVars = () => {
    const box = boxOf();
    if (!box) return;
    for (const v of ['--modal-dx', '--modal-dy', '--modal-sx', '--modal-sy']) box.style.removeProperty(v);
  };
  // Drop the closing shape immediately (also called when a re-open interrupts it).
  const finishClose = () => {
    if (closeTimer) { clearTimeout(closeTimer); closeTimer = null; }
    overlay.classList.remove('modal-closing', 'modal-popover');
    const box = boxOf();
    if (box) { box.style.left = ''; box.style.top = ''; }
    clearOriginVars();
  };
  // Both flights start here, so a superseding open/close always drops the one in the
  // air (settleSurface) instead of leaving a cloud or a veiled box behind.
  const playDust = (enter) => {
    const box = boxOf();
    if (!box) return;
    if (reducedMotion()) { settleSurface(box); return; }
    (enter ? surfaceIn : surfaceOut)(box, originPoint);
  };
  // The close half, as every caller plays it: measure while the window is still up,
  // hand over to the cloud, and wear `modal-closing` for exactly the flight.
  const playClosing = () => {
    overlay.classList.add('modal-closing');
    playDust(false);
    if (closeTimer) clearTimeout(closeTimer);
    closeTimer = setTimeout(() => { closeTimer = null; finishClose(); }, MODAL_CLOSE_MS);
  };
  return { reducedMotion, setOrigin, finishClose, playDust, playClosing,
           settle: () => settleSurface(boxOf()) };
};
