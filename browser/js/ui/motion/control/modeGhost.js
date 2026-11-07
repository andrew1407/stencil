// Sliding's stand-in for a control group's cloud (desktop support/control/reveal/modeGhost.cpp): the group's
// picture slides whole along its axis inside the box it stands in, the group itself hidden under it while its
// slot opens or shuts. Sliding only the children inside the shrinking slot squeezed the group (user report).
import { motionMs } from '../motionPrefs.js';
import { snapshot } from '../genie/shot.js';

const EASE_IN = 'cubic-bezier(0.16, 1, 0.3, 1)';
const EASE_OUT = 'cubic-bezier(0.55, 0, 0.75, 0.4)';
const runs = new WeakMap();   // group → its latest slide; a picture landing for an older one is dropped
const ends = new WeakMap();   // group → the end of the slide in flight

// `r` (a client rect) is the group at its natural size; `opening` slides it in from its leading edge, else out
// through it; `vertical` picks the axis. False when nothing can play (no DOM, no box, no Web Animations).
export function slideGhost(el, r, { opening, vertical = false, ms }) {
  if (typeof document === 'undefined' || !el?.style || !(r?.width > 0 && r?.height > 0)) return false;
  if (typeof document.createElement('div').animate !== 'function') return false;
  const run = (runs.get(el) ?? 0) + 1;
  runs.set(el, run);
  const shot = snapshot(el);   // styles read now, while the group still stands at its natural size
  el.style.visibility = 'hidden';
  const clip = document.createElement('div');
  Object.assign(clip.style, { position: 'fixed', left: `${r.left}px`, top: `${r.top}px`, width: `${r.width}px`,
    height: `${r.height}px`, overflow: 'hidden', pointerEvents: 'none', zIndex: '60' });
  document.body.appendChild(clip);
  const span = motionMs(ms);
  const t0 = performance.now();
  const end = () => {
    clip.remove();
    if (runs.get(el) === run) el.style.visibility = '';
    if (ends.get(el) === end) ends.delete(el);   // the clip and its picture go with the closure
  };
  ends.set(el, end);
  shot.then((canvas) => {
    if (!canvas || runs.get(el) !== run) { end(); return; }
    Object.assign(canvas.style, { width: '100%', height: '100%', display: 'block' });
    clip.appendChild(canvas);
    const away = vertical ? 'translateY(-100%)' : 'translateX(-100%)';
    const frames = opening ? [{ transform: away }, { transform: 'none' }] : [{ transform: 'none' }, { transform: away }];
    // The picture arrives a frame or two late: it joins the run where the slot already is.
    const anim = canvas.animate(frames, { duration: span, easing: opening ? EASE_IN : EASE_OUT, fill: 'both' });
    anim.currentTime = Math.min(span, performance.now() - t0);
    anim.finished.then(end, end);
  }, end);
  setTimeout(end, span + 400);   // a picture that never lands still gives the group back
  return true;
}

// Drops the slide in flight, if any: its picture goes and the group shows again.
export function settleSlideGhost(el) {
  if (!el) return;
  ends.get(el)?.();
  ends.delete(el);
  runs.set(el, (runs.get(el) ?? 0) + 1);
}
