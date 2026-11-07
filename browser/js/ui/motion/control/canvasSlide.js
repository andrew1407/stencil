// A picture arriving or leaving under the two non-dust modes: Sliding pours it out of (and back into)
// the bottom edge of the view, or the drop point, as a window pours (motion/genie/); Transition
// wipes it in from the view's bottom edge upward and away from the top down. Both stay inside the
// view, however far the picture overflows it. Desktop twin: support/genie/canvasSlide.cpp.
import { motionMode, motionReduced } from '../motionPrefs.js';
import { genieIn, genieShotAway, pointBox, settleGenie } from '../genie/window.js';
import { GENIE_OPEN_MS, followDrift } from '../genie/flight.js';
import { flashLanding } from '../enterLeave.js';

export const RISE_CLASS = 'canvas-rise';
export const SINK_CLASS = 'canvas-sink';
const RISE_MS = 413;   // ms, animations/dust.css canvasRise / canvasSink (user report)
// A picture pours in 1.5x a window's open, so it reads as arriving (desktop canvasSlide.cpp ARRIVE_MS).
export const CANVAS_ARRIVE_MS = GENIE_OPEN_MS * 1.5;

// The strip a picture pours out of and back into: the middle of the view's bottom edge.
export const bottomStrip = (vp) => {
  const r = vp?.getBoundingClientRect?.();
  return r && r.width > 0 ? { left: r.left + r.width / 2 - 40, top: r.bottom - 2, width: 80, height: 2 } : null;
};

// The drop point only while it lies in the view: a dialog's button below it poured the picture up
// out of the bars under the canvas (user report).
const inView = (p, vp) => {
  const r = vp?.getBoundingClientRect?.();
  return p && r && p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom ? p : null;
};

const mode = () => (motionReduced() ? 'none' : motionMode());

// The page scrolled so the view is on screen, at once, before a flight measures it: stacked under a narrow
// window the view sits below the toolbar's fold, and a picture poured there played unseen (user report).
export const showView = (vp) => {
  try { vp?.scrollIntoView?.({ block: 'nearest', inline: 'nearest', behavior: 'instant' }); } catch { /* decoration */ }
};

// The view's client rect, or null: every flight is clipped to it (a docked chat narrows the view, and the
// picture that then overflows it poured over the bars below).
const viewRect = (vp) => {
  const r = vp?.getBoundingClientRect?.();
  return r && r.width > 0 && r.height > 0 ? { left: r.left, top: r.top, width: r.width, height: r.height } : null;
};

// The arrival by mode: Sliding pours the picture out of the drop point or the bottom strip; Transition wipes it
// up from the VISIBLE bottom (a picture taller than the view would rise unseen below it).
const ARRIVE = Object.freeze({
  slide: (box, vp, from) =>
    genieIn(box, pointBox(inView(from, vp)) || bottomStrip(vp), { ms: CANVAS_ARRIVE_MS, clip: viewRect(vp), keep: false, follow: vp }),
  transition: (box, vp) => {
    settleGenie(box);   // a hold left by an earlier Sliding arrival would still the rise
    const b = box.getBoundingClientRect?.();
    const v = viewRect(vp);
    if (b && v && b.height > 0) {
      const shown = Math.min(b.bottom, v.top + v.height);
      box.style.setProperty('--wipe-top', `${Math.max(0, shown - b.top)}px`);
      box.style.setProperty('--wipe-bottom', `${Math.max(0, b.bottom - shown)}px`);
    }
    flashLanding(box, RISE_CLASS, RISE_MS);
    return true;
  },
});

// True when the arrival played here; false leaves it to the caller's own landing.
export function slideCanvasIn(box, vp, from = null) {
  if (!box?.classList) return false;
  return (ARRIVE[mode()] ?? (() => false))(box, vp, from);
}

// The part of the picture the view shows, a client rect, or null.
const shownRect = (box, vp) => {
  const b = box.getBoundingClientRect();
  const v = viewRect(vp) || b;
  const left = Math.max(b.left, v.left), top = Math.max(b.top, v.top);
  const r = { left, top, width: Math.min(b.right, v.left + v.width) - left, height: Math.min(b.bottom, v.top + v.height) - top };
  return r.width > 0 && r.height > 0 ? r : null;
};

// The canvases' own pixels composed into one picture of `r` at device resolution, drawn straight across: a DOM copy
// of them cost three rasters (encode, decode, snapshot), and a tainted layer dropped out of it.
const pictureShot = (box, r) => {
  const scale = globalThis.devicePixelRatio || 1;
  const shot = document.createElement('canvas');
  shot.width = Math.max(1, Math.round(r.width * scale));
  shot.height = Math.max(1, Math.round(r.height * scale));
  const ctx = shot.getContext('2d');
  if (!ctx) return null;
  for (const c of box.querySelectorAll('canvas')) {
    const cr = c.getBoundingClientRect();
    if (c.width > 0 && c.height > 0 && cr.width > 0 && cr.height > 0)
      ctx.drawImage(c, (cr.left - r.left) * scale, (cr.top - r.top) * scale, cr.width * scale, cr.height * scale);
  }
  return shot;
};

// A fixed copy rides with the view (the toolbar re-wrapping above it moves the view mid-wipe), frame by frame until it goes.
const rideWith = (el, vp) => {
  if (typeof vp?.getBoundingClientRect !== 'function' || typeof requestAnimationFrame !== 'function') return;
  const drift = followDrift(vp);
  const step = () => {
    if (!el.isConnected) return;
    el.style.transform = drift();
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
};

// `shot` fixed where the picture stood, wiped away top down (animations/dust.css canvasSink), then gone.
const sinkCopy = (shot, r, vp) => {
  const copy = document.createElement('div');
  Object.assign(copy.style, { position: 'fixed', left: `${r.left}px`, top: `${r.top}px`, width: `${r.width}px`,
    height: `${r.height}px`, overflow: 'hidden', pointerEvents: 'none', zIndex: '50' });
  Object.assign(shot.style, { width: '100%', height: '100%', display: 'block' });
  copy.classList.add(SINK_CLASS);
  copy.append(shot);
  document.body.append(copy);
  rideWith(copy, vp);
  const done = () => copy.remove();
  copy.addEventListener('animationend', done, { once: true });
  setTimeout(done, RISE_MS + 200);
};

// The leave by mode: Sliding pours the picture into the bottom strip, Transition wipes a copy away.
const LEAVE = Object.freeze({
  slide: (shot, r, vp) => genieShotAway(shot, r, bottomStrip(vp), { clip: viewRect(vp), follow: vp }),
  transition: (shot, r, vp) => { sinkCopy(shot, r, vp); return true; },
});

// Call BEFORE the canvas is cleared; true when the leaving picture is on its way.
export function slideCanvasOut(box, vp) {
  if (!box?.classList || typeof document === 'undefined') return false;
  showView(vp);
  const leave = LEAVE[mode()];
  if (!leave) return false;
  const r = shownRect(box, vp);
  const shot = r && pictureShot(box, r);
  return !!shot && leave(shot, r, vp);
}

// The same exit, found from the canvas itself (its container and the view it scrolls in).
export const slideCanvasAway = (canvas) =>
  slideCanvasOut(canvas?.closest?.('#canvas-container'), canvas?.closest?.('#canvas-viewport'));
