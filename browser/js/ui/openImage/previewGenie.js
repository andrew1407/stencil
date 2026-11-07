// The Open-Image picture's flight where no particles fly, between it and the control that loaded it
// (Choose File, or Preview): Sliding pours it, as every window pours; Transition grows it out of that
// control's box. Desktop twin: chipDust.cpp mediaFlight.
import { genieEnabled } from '../motion/genie/window.js';
import { playGenie, GENIE_OPEN_MS, GENIE_CLOSE_MS } from '../motion/genie/flight.js';
import { motionMode, motionMs, motionReduced } from '../motion/motionPrefs.js';
import { BOX_RESIZE_MS as GROW_MS } from '../motion/easeBoxHeight.js';

const cancels = new WeakMap();   // picture → the cancel of its flight in progress

// The frame as it shows, drawn straight off the media: a cloned <video> paints nothing in a DOM picture.
const mediaShot = (el, r) => {
  const scale = globalThis.devicePixelRatio || 1;
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.round(r.width * scale));
  c.height = Math.max(1, Math.round(r.height * scale));
  try { c.getContext('2d').drawImage(el, 0, 0, c.width, c.height); } catch { return null; }
  return c;
};

const pour = (el, win, icon, opening, done) => {
  const shot = mediaShot(el, win);
  if (!shot) return 0;
  const ms = motionMs(opening ? GENIE_OPEN_MS : GENIE_CLOSE_MS);
  el.style.opacity = '0';
  cancels.set(el, playGenie(shot, win, icon, { opening, ms, done: () => {
    cancels.delete(el);
    if (opening) el.style.opacity = '';
    done?.();
  } }));
  return ms;
};

// The box the control stands in, as a transform of the picture's own (origin top-left).
const grow = (el, win, icon, opening, done) => {
  const sx = icon.width / win.width, sy = icon.height / win.height;
  const away = { opacity: 0, transform: `translate(${icon.left - win.left}px, ${icon.top - win.top}px) scale(${sx}, ${sy})` };
  const home = { opacity: 1, transform: 'none' };
  const ms = motionMs(GROW_MS);
  el.style.opacity = '';
  const f = el.animate(opening ? [away, home] : [home, away], {
    duration: ms, fill: opening ? 'backwards' : 'forwards',
    easing: opening ? 'cubic-bezier(0.22,0.61,0.36,1)' : 'cubic-bezier(0.55,0,0.75,0.4)' });
  el.style.transformOrigin = '0 0';
  cancels.set(el, () => f.cancel());
  f.finished.then(() => {
    cancels.delete(el);
    if (!opening) el.style.opacity = '0';
    f.cancel();
    el.style.transformOrigin = '';
    done?.();
  }, () => { el.style.transformOrigin = ''; });
  return ms;
};

// Flies `el` out of (`opening`) or into `from`; its length in ms, or 0 in any other mode.
export const flyMedia = (el, from, opening, done = null) => {
  if (motionReduced() || !el?.getBoundingClientRect) return 0;
  cancels.get(el)?.();   // one flight per picture: a swap mid-arrival takes over from it
  const isSlide = genieEnabled();
  if (!isSlide && (motionMode() !== 'transition' || !el.animate)) return 0;
  const win = el.getBoundingClientRect();
  const icon = from?.getBoundingClientRect?.();
  if (!(win.width > 0 && win.height > 0 && icon?.width > 0)) return 0;
  return isSlide ? pour(el, win, icon, opening, done) : grow(el, win, icon, opening, done);
};

// Ends a flight `flyMedia` started on `el`, leaving it shown.
export const cancelMediaFlight = (el) => {
  if (!el) return;
  cancels.get(el)?.();
  cancels.delete(el);
  el.style.opacity = '';
};
