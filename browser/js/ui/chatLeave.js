// The chat leave clocks.
import { CHAT_LEAVE_MS, CHIP_LEAVE_MS, ITEM_DUST_MS, leaveThenRemove, scatterGridFor } from './motion.js';


// `count` rows going at once share one tile budget (motion.js scatterGridFor).
export const chatLeave = (el, done, count = 1, index = 0) =>
  leaveThenRemove(el, done, { ms: CHAT_LEAVE_MS, dustMs: ITEM_DUST_MS, ...scatterGridFor(count, index) });
// Chips ride the longer chip clock (css chipLeave hold + collapse).
export const chipLeave = (el, done) =>
  leaveThenRemove(el, done, { ms: CHIP_LEAVE_MS, ...scatterGridFor(1, 0) });
// How the empty attachments row waits out the last chip's dust: a cloud is torn down a
// beat after its wipe, and it lives in the row it left.
export const ATTACH_SETTLE_STEP_MS = 120;
export const ATTACH_SETTLE_TRIES = 12;
