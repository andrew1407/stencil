// The picture's quarter turn: the rebuilt, refitted picture starts turned back onto the box the
// old one filled and eases upright. Extension port: src/lib/motion/quarterTurn.js; desktop twin:
// canvas/overlay/QuarterTurnOverlay.hpp.
import { motionReduced } from './motionPrefs.js';

// The start transform for a box `to` that must first cover `from` turned by -dir·90° (dir > 0 = CW).
export function quarterTurnStart(from, to, dir) {
  if (!(from?.width > 0 && from?.height > 0 && to?.width > 0 && to?.height > 0)) return null;
  const s = from.width / to.height;
  const dx = (from.left + from.width / 2) - (to.left + to.width / 2);
  const dy = (from.top + from.height / 2) - (to.top + to.height / 2);
  return `translate(${dx}px, ${dy}px) rotate(${dir > 0 ? -90 : 90}deg) scale(${s})`;
}

// Measure `box` before the change; play(dir) after its relayout turns it in from that box. `ms` and
// `easing` are motion.json ROTATE_MS / ROTATE_EASING; `viewport` hides its overflow while it turns.
export function beginQuarterTurn(box, { ms, easing, viewport = null, onStart, onEnd } = {}) {
  if (!box?.animate || motionReduced()) return { play() {} };
  box._quarterTurn?.finish();
  const from = box.getBoundingClientRect();
  onStart?.();
  return {
    play(dir) {
      const start = quarterTurnStart(from, box.getBoundingClientRect(), dir);
      const done = () => {
        if (viewport) viewport.style.overflow = '';
        box._quarterTurn = null;
        onEnd?.();
      };
      if (!start) { done(); return; }
      if (viewport) viewport.style.overflow = 'hidden';
      box.style.transformOrigin = '50% 50%';
      const end = 'translate(0px, 0px) rotate(0deg) scale(1)';
      const anim = box.animate([{ transform: start }, { transform: end }], { duration: ms, easing });
      box._quarterTurn = anim;
      anim.onfinish = anim.oncancel = done;
    },
  };
}
