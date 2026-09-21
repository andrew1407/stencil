// Shared rig for the popover specs: a clock whose timers fire only when advanced, plus the
// deferring and eager modal-open gesture machines. Extracted from popover.test.js.
import { createModalOpenGesture } from '../../js/ui/tip/popover.js';

export const stubTimers = () => {
  const jobs = new Map();
  let seq = 0;
  return {
    setTimer: (fn, ms) => { jobs.set(++seq, { fn, ms }); return seq; },
    clearTimer: (id) => jobs.delete(id),
    get pending() { return jobs.size; },
    advance(ms) {
      for (const [id, job] of [...jobs]) {
        if (job.ms <= ms) { jobs.delete(id); job.fn(); }
      }
    },
  };
};

export const machine = () => {
  const timers = stubTimers();
  const calls = [];
  const g = createModalOpenGesture({
    openFull: () => calls.push('full'),
    openPopover: () => calls.push('popover'),
    setTimer: timers.setTimer,
    clearTimer: timers.clearTimer,
  });
  return { timers, calls, g };
};

// An eager machine acts on the FIRST click: the wait exists so a full modal never flashes open and shut
// under a double-click, which a panel that only re-shapes the same window has no need of.
export const eagerMachine = () => {
  const timers = stubTimers();
  const calls = [];
  const g = createModalOpenGesture({
    openFull: () => calls.push('full'),
    openPopover: () => calls.push('popover'),
    eagerClick: true,
    setTimer: timers.setTimer,
    clearTimer: timers.clearTimer,
  });
  return { timers, calls, g };
};
