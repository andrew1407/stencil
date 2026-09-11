// ── The chat leave clocks ───────────────────────────────────────
// Every chat entry leaves on the same dissolve; the chips ride a longer clock.
import { CHAT_LEAVE_MS, CHIP_LEAVE_MS, ITEM_DUST_MS, leaveThenRemove, scatterGridFor } from './motion.js';


// Every chat entry leaves on the same dissolve. `count` is how many are going at once
// — one removal gets the full fine mesh, a whole-transcript wipe coarsens so the total
// number of flying tiles stays inside the budget (motion.js scatterGridFor).
export const chatLeave = (el, done, count = 1, index = 0) =>
  leaveThenRemove(el, done, { ms: CHAT_LEAVE_MS, dustMs: ITEM_DUST_MS, ...scatterGridFor(count, index) });
// (…and the mirror, motion.js chatIn, is played by renderChatLog on every entry that
// APPEARS — a fresh row, or a pending "…" resolving into the answer.)
// Chips ride the longer chip clock (css chipLeave hold + collapse — see motion.js).
export const chipLeave = (el, done) =>
  leaveThenRemove(el, done, { ms: CHIP_LEAVE_MS, ...scatterGridFor(1, 0) });
// How the empty attachments row waits out the last chip's dust (see below): after the
// wipe, then every step until the layer is gone — a cloud is torn down a beat after the
// wipe it belongs to, and it lives in the row it left.
export const ATTACH_SETTLE_STEP_MS = 120;
export const ATTACH_SETTLE_TRIES = 12;
