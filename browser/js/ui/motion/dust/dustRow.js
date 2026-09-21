// A row arriving/leaving on the KEYWORD-CHIP recipe (tiles.js CHIP_*): the particles carry
// the line while the row's height slides under them. Desktop twin: slideCropDims.
import { motionReduced } from '../../motionPrefs.js';
import { disintegrate } from '../disintegrate.js';
import { speckPainter } from '../surface/painters.js';
import { BOX_RESIZE_MS } from '../easeBoxHeight.js';
import { CHIP_MOTE_PX, CHIP_DUST_MS, CHIP_DUST_DRIFT, chipGrid } from '../surface/tiles.js';

export const CHIP_DUST = Object.freeze({ ms: CHIP_DUST_MS, drift: CHIP_DUST_DRIFT,
                                         px: CHIP_MOTE_PX, ...chipGrid(1) });

// The cloud follows by DELTA, keeping the offset a cloud given its own `box` has — tiles.js
// retargetDust pins absolutely instead and would lose it.
export const followCloud = (el) => {
  const host = el.__dustHost;
  if (!host) return true;
  const r0 = el.getBoundingClientRect();
  const left0 = parseFloat(host.style.left) || 0, top0 = parseFloat(host.style.top) || 0;
  const tick = () => {
    if (el.__dustHost !== host) return;   // disintegrate clears it when the flight ends
    const r = el.getBoundingClientRect();
    host.style.left = `${left0 + r.left - r0.left}px`;
    host.style.top = `${top0 + r.top - r0.top}px`;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
  return true;
};

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
                         { duration: BOX_RESIZE_MS, easing: 'cubic-bezier(0.22,0.61,0.36,1)', fill: 'both' });
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
    }) && (!gather || followCloud(el));   // a fall stays where the row stood
    if (show) {
      el.style.display = display;
      el.style.opacity = '0';   // while they fly, the motes ARE the line
      if (cloud(true)) setTimeout(() => { el.style.opacity = ''; }, CHIP_DUST_MS);
      else el.style.opacity = '';
    } else {
      cloud(false);
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
      ...CHIP_DUST, gather, toBody: true,
      hostClass: gather ? 'dust-forming' : 'dust-falling',
      paintTile: speckPainter(el),
    }) && followCloud(el);
    if (show) {
      el.style.visibility = 'visible';
      el.style.opacity = '0';
      if (cloud(true)) setTimeout(() => { el.style.opacity = ''; }, CHIP_DUST_MS);
      else el.style.opacity = '';
    } else {
      cloud(false);
      el.style.opacity = '0';
      setTimeout(() => { el.style.visibility = 'hidden'; el.style.opacity = ''; }, CHIP_DUST_MS);
    }
  };
};
