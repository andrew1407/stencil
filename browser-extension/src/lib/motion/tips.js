// A hover tip's clocks and the point its dust belongs to. A hover TIP is brisker still: re-triggered mid-sweep,
// its flight must be over before the next begins. The clocks are common/config/motion.json's TIP_* values, held
// to the extension's copy by tests/lib/motion/motionClocks.test.js; browser twin: ui/motion/control/tips.js.
import { motionMode, motionReduced } from '../prefs/motionPrefs.js';
import { genieIn, genieAway, pointBox } from './surfaces.js';
import { SURFACE_DRIVEN_CLASS } from './surfaceMotion.js';
import { flashLanding } from './enterLeave.js';

export const TIP_DUST_IN_MS = 260;
export const TIP_DUST_OUT_MS = 190;
// …and wakes on one delay across surfaces (desktop SnappyTooltipStyle, main.cpp).
export const TIP_SHOW_DELAY_MS = 200;

// Where no particles fly: Sliding pours the tip out of (and back into) its point, Transition grows it
// there and shrinks it back (browser motion/control/tips.js). In returns its run (ms, 0 = none).
export const TIP_GROW_CLASS = 'tip-grow-in';
export const TIP_SHRINK_CLASS = 'tip-shrink-out';
// ms: animations/overlays.css stTipGrowIn / stTipShrinkOut, and Sliding's pour, half again the grow.
const TIP_GROW_MS = 220;
const TIP_SHRINK_MS = 180;
const TIP_SLIDE_IN_MS = 330;
const TIP_SLIDE_OUT_MS = 270;
const TIP_IN_MS = Object.freeze({ transition: TIP_GROW_MS });   // the grow where no pour plays; 0 in any other mode
// `landed` runs once the tip is seen, after its flight (the extension's genieIn takes no `landed` of its own).
export const tipFallbackIn = (el, point, landed = null) => {
  if (!el?.classList || !point || motionReduced()) return false;
  // Its own fade steps aside first: the pour photographs the tip at rest (browser motion/control/tips.js).
  el.classList.add(SURFACE_DRIVEN_CLASS);
  const isPoured = genieIn(el, pointBox(point), { ms: TIP_SLIDE_IN_MS });
  const ms = isPoured ? TIP_SLIDE_IN_MS : (TIP_IN_MS[motionMode()] ?? 0);
  if (!isPoured && ms) { el.classList.remove(TIP_SHRINK_CLASS); flashLanding(el, TIP_GROW_CLASS, ms); }
  if (ms && landed) setTimeout(landed, ms);   // the extension's clock is unscaled (lib/motion.js motionMs)
  return ms;
};
// Transition shrinks the tip itself, so it stays laid out while it hides: a copy loses the id its look hangs on.
const TIP_OUT = Object.freeze({
  transition: (el) => { el.classList.remove(TIP_GROW_CLASS); flashLanding(el, TIP_SHRINK_CLASS, TIP_SHRINK_MS); return true; },
});
const pourOut = (el, point) => genieAway(el, pointBox(point), { ms: TIP_SLIDE_OUT_MS });
export const tipFallbackOut = (el, point) =>
  !!el?.classList && !!point && !motionReduced() && (TIP_OUT[motionMode()] ?? pourOut)(el, point);

// The centre of an element (or rect): the point a popup's dust belongs to. Null for a
// detached owner — the flight then settles instead.
export const rectCenter = (elOrRect) => {
  const r = typeof elOrRect?.getBoundingClientRect === 'function'
    ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !(r.width > 0 || r.height > 0)) return null;
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
};
