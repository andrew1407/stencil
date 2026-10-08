// The picture's left-right flip: the rebuilt picture starts mirrored back (scaleX -1, which is the
// old picture exactly) and turns over through edge-on to face the user. Desktop twin:
// canvas/overlay/QuarterTurnOverlay.hpp (its flip mode).
import { motionReduced } from './motionPrefs.js';

// `ms` and `easing` are motion.json ROTATE_MS / ROTATE_EASING, the quarter turn's own clock.
export function beginMirrorFlip(box, { ms, easing, onStart, onEnd } = {}) {
  if (!box?.animate || motionReduced()) return { play() {} };
  box._mirrorFlip?.finish();
  onStart?.();
  return {
    play() {
      box.style.transformOrigin = '50% 50%';
      const anim = box.animate([{ transform: 'scaleX(-1)' }, { transform: 'scaleX(1)' }], { duration: ms, easing });
      box._mirrorFlip = anim;
      anim.onfinish = anim.oncancel = () => { box._mirrorFlip = null; onEnd?.(); };
    },
  };
}
