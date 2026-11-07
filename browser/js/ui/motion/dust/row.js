// A row arriving/leaving on the KEYWORD-CHIP recipe (tiles.js CHIP_*): the particles carry
// the line while the row's height slides under them. Desktop twin: slideCropDims.
import { motionMode, motionMs, motionReduced } from '../motionPrefs.js';
import { disintegrate } from '../disintegrate.js';
import { speckPainter } from '../surface/painters.js';
import { BOX_RESIZE_MS } from '../easeBoxHeight.js';
import { CHIP_MOTE_PX, CHIP_DUST_MS, CHIP_DUST_DRIFT, chipGrid } from '../surface/tiles.js';

export const CHIP_DUST = Object.freeze({ ms: CHIP_DUST_MS, drift: CHIP_DUST_DRIFT,
                                         px: CHIP_MOTE_PX, ...chipGrid(1) });

// Sliding and Transition fly no cloud, so the element itself slides out of its left edge (clipped
// to its own box) or grows from 0.78, as a list row does (reveal.css rowSlideLeave / rowShrinkLeave).
const FLIGHT_FRAMES = Object.freeze({
  slide: [{ transform: 'translateX(-104%)', clipPath: 'inset(0 0 0 104%)' },
          { transform: 'none', clipPath: 'inset(0 0 0 0)' }],
  transition: [{ opacity: 0, transform: 'scale(0.78)' }, { opacity: 1, transform: 'none' }],
});
const flights = new WeakMap();
// The Animation, or null when the mode flies a cloud (or nothing). A hide holds its last frame.
export const modeFlight = (el, show, ms = BOX_RESIZE_MS) => {
  const frames = FLIGHT_FRAMES[motionMode()];
  flights.get(el)?.cancel();
  if (!frames || !el?.animate || motionReduced()) return null;
  const f = el.animate(show ? frames : [...frames].reverse(), {
    duration: motionMs(ms), fill: show ? 'backwards' : 'forwards',
    easing: show ? 'cubic-bezier(0.16,1,0.3,1)' : 'cubic-bezier(0.55,0,0.75,0.4)' });
  flights.set(el, f);
  return f;
};
export const cancelModeFlight = (el) => { flights.get(el)?.cancel(); flights.delete(el); };

/**
 * One flight per row, so two rows fly together without fighting each other's state. `dustEl`
 * scopes the cloud SMALLER than the sliding row — a `<div>` is as wide as its container, so a
 * label-and-control row would scatter its trailing space too. Read at flight time: which
 * control changed depends on the row's state.
 */
export const makeDustRow = (el, display = 'block', dustEl = () => el) => {
  let shown = false, flight = null;
  // The row SLIDES rather than appearing: display:none snaps everything under it, and the
  // row's own padding must collapse with it or its box still shows once "hidden".
  const padTop = parseFloat(getComputedStyle(el).paddingTop) || 0;
  const padBottom = parseFloat(getComputedStyle(el).paddingBottom) || 0;
  const slide = (show) => {
    // Only a COLUMN's row gap folds away with it — on a line, it would lift the group.
    const ps = getComputedStyle(el.parentElement);
    const gap = ps.flexDirection === 'column' ? (parseFloat(ps.rowGap) || 0) : 0;
    const done = () => {
      for (const k of ['height', 'marginTop', 'overflow', 'paddingTop', 'paddingBottom'])
        el.style.removeProperty(k);
      if (!show) el.style.display = 'none';
    };
    if (!el.animate || motionReduced()) { el.style.display = show ? display : 'none'; return; }
    flight?.cancel();
    el.style.display = display;
    el.style.overflow = 'hidden';
    const full = el.scrollHeight;
    const box = (h, t) => ({ height: `${h}px`, marginTop: `${h ? 0 : -gap}px`,
                             paddingTop: `${t ? padTop : 0}px`, paddingBottom: `${t ? padBottom : 0}px` });
    const f = el.animate([box(show ? 0 : full, show ? 0 : 1), box(show ? full : 0, show ? 1 : 0)],
                         { duration: motionMs(BOX_RESIZE_MS), easing: 'cubic-bezier(0.22,0.61,0.36,1)', fill: 'both' });
    flight = f;
    f.finished.then(() => { if (flight === f) { flight = null; f.cancel(); done(); } }, () => {});
  };
  return (show, animate = false) => {
    if (show === shown) return;
    shown = show;
    if (!animate) { el.style.display = show ? display : 'none'; return; }
    // ONE motion: the particles carry the line, the BOX eases into its space once
    // (easeBoxHeight) — as two steps it left a plateau between them.
    const cloud = (gather) => disintegrate(el, {
      ...CHIP_DUST, gather, toBody: true,
      hostClass: gather ? 'dust-forming' : 'dust-falling',
      paintTile: speckPainter(dustEl()),
      box: dustEl() === el ? null : dustEl().getBoundingClientRect(),
      anchor: gather ? el : null,   // a fall stays where the row stood
    });
    if (show) {
      el.style.display = display;
      el.style.opacity = '0';   // while they fly, the motes ARE the line
      if (cloud(true)) setTimeout(() => { el.style.opacity = ''; }, motionMs(CHIP_DUST_MS));
      else { el.style.opacity = ''; modeFlight(el, true); }
    } else if (!cloud(false)) {
      modeFlight(el, false)?.finished.then(() => { if (!shown) cancelModeFlight(el); }, () => {});
    }
    slide(show);
  };
};

/**
 * The same cloud for an INLINE control, whose row stays open either way — so no slide.
 * `display` stays on permanently (disintegrate needs the laid-out rect to scatter from) and
 * `visibility` carries the show/hide, or the row's wrap gains the control's SPACE a frame
 * before any mote appears. On hide it fades WITH the cloud, not under it.
 */
export const makeDustToggle = (el, display = 'inline-flex') => {
  let shown = false;
  el.style.display = display;
  el.style.visibility = 'hidden';
  return (show, animate = false) => {
    if (show === shown) return;
    shown = show;
    if (!animate) {
      el.style.visibility = show ? 'visible' : 'hidden';
      el.style.opacity = show ? '' : '0';
      return;
    }
    const cloud = (gather) => disintegrate(el, {
      ...CHIP_DUST, gather, toBody: true, anchor: el,
      hostClass: gather ? 'dust-forming' : 'dust-falling',
      paintTile: speckPainter(el),
    });
    if (show) {
      el.style.visibility = 'visible';
      el.style.opacity = '0';
      if (cloud(true)) setTimeout(() => { el.style.opacity = ''; }, motionMs(CHIP_DUST_MS));
      else { el.style.opacity = ''; modeFlight(el, true, CHIP_DUST_MS); }
      return;
    }
    const hide = () => { if (shown) return; el.style.visibility = 'hidden'; el.style.opacity = ''; cancelModeFlight(el); };
    if (cloud(false)) {
      el.style.opacity = '0';
      setTimeout(hide, motionMs(CHIP_DUST_MS));
      return;
    }
    const f = modeFlight(el, false, CHIP_DUST_MS);
    if (f) f.finished.then(hide, () => {});
    else { el.style.opacity = '0'; setTimeout(hide, motionMs(CHIP_DUST_MS)); }
  };
};
