// The chat leave clocks.
import { CHAT_LEAVE_MS, CHIP_LEAVE_MS, leaveThenRemove } from '../motion/enterLeave.js';
import { ITEM_DUST_MS, scatterGridFor } from '../motion/surface/tiles.js';
import { motionMode } from '../motion/motionPrefs.js';
import { genieAway, MENU_GENIE_MS } from '../motion/genie/window.js';
import { chatTailHome } from '../motion/control/chatFx.js';


// `count` rows going at once share one tile budget (motion.js scatterGridFor).
// Sliding pours each one back into its tail home (chatFx.js chatTailHome), its row held hidden till it goes.
export const chatLeave = (el, done, count = 1, index = 0) => {
  const view = el?.parentElement?.getBoundingClientRect?.();
  if (motionMode() === 'slide' && genieAway(el, chatTailHome(el, el?.getBoundingClientRect?.(), view), { ms: MENU_GENIE_MS, clip: view }))
    el.style.visibility = 'hidden';
  return leaveThenRemove(el, done, { ms: CHAT_LEAVE_MS, dustMs: ITEM_DUST_MS, ...scatterGridFor(count, index) });
};
// Chips ride the longer chip clock (css chipLeave hold + collapse).
export const chipLeave = (el, done) =>
  leaveThenRemove(el, done, { ms: CHIP_LEAVE_MS, ...scatterGridFor(1, 0) });
// How the empty attachments row waits out the last chip's dust: a cloud is torn down a
// beat after its wipe, and it lives in the row it left.
export const ATTACH_SETTLE_STEP_MS = 120;
export const ATTACH_SETTLE_TRIES = 12;
