// The reveal sweep's pace, a port of cli/src/console/render/logoFx/reveal.zig: new output types
// in from the left in jumps; a BURST (output within quietMs of the last) sweeps in fewer jumps,
// and past burstMaxMs of pouring output lands at once. Every window scales off the speed.
import FX from '../../../../common/config/console/fx.json' with { type: 'json' };
import { CONSOLE_SPEED } from '../motion/motionPrefs.js';

const R = FX.reveal;

// `factor` is 1 at the default speed; a window never drops under its floor or past the ceiling.
const scale = (base, k, floor) => {
  const v = Math.round(base * k);
  if (!(v > floor)) return floor;
  return Math.min(v, R.ceilingMs);
};

export const pace = (speed) => {
  const s = Math.min(CONSOLE_SPEED.max, Math.max(CONSOLE_SPEED.min, Number(speed) || CONSOLE_SPEED.default));
  const k = (1 - s) / s;
  return {
    stepMs: scale(R.stepMs, k, 1),
    // The quiet window must outlast one line's own sweep, or every burst line gets the long form.
    quietMs: scale(R.quietMs, k, R.quietMs),
    burstMaxMs: scale(R.burstMaxMs, k, R.burstMaxMs),
    instant: s >= CONSOLE_SPEED.max,
  };
};

// Tracks the bursts: `plan(now)` says how the output arriving now is swept — `jumps` 0 = at once.
export const createReveal = ({ speed = () => CONSOLE_SPEED.default } = {}) => {
  let lastMs = 0;
  let burstMs = 0;
  return {
    plan(now) {
      const p = pace(speed());
      const inBurst = lastMs !== 0 && now - lastMs <= p.quietMs;
      if (!inBurst) burstMs = now;
      lastMs = now;
      if (p.instant || (inBurst && now - burstMs > p.burstMaxMs)) return { jumps: 0, stepMs: 0 };
      return { jumps: inBurst ? R.burstJumps : R.jumps, stepMs: p.stepMs };
    },
    // A sweep ended: the quiet window counts from here, as reveal.zig stamps it on the way out.
    done(now) { lastMs = now; },
    reset() { lastMs = 0; burstMs = 0; },
  };
};
