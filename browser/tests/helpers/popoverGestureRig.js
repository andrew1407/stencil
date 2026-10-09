// Shared rig for the popover specs: a clock whose timers fire only when advanced, plus the
// deferring and eager modal-open gesture machines. Extracted from popover.test.js.
import { createModalOpenGesture } from '../../js/ui/tip/popover.js';
import { stubTimers } from './timers.js';

export { stubTimers };


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
