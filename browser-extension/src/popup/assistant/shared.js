// ── Shared assistant helpers ────────────────────────────────────────────────
// The transcript's arrive/leave gestures and the two small entry adapters, used by
// assistant.js and the modules beside it.
import { chatIn, leaveThenRemove, CHAT_LEAVE_MS, scatterGridFor } from '../../lib/motion.js';
import { rasterizeToPngDataUrl } from '../../lib/rasterize.js';
import { splitDataUrl, MAX_IMAGE_EDGE } from '../../llm/chatController.js';

// `count` is how many entries leave at once: one removal gets the full fine mesh, a whole-
// transcript clear coarsens so the flying tiles stay inside lib/motion.js's budget.
export const chatLeave = (el, done, count = 1, index = 0) =>
  leaveThenRemove(el, done, { ms: CHAT_LEAVE_MS, ...scatterGridFor(count, index) });
// Called AFTER scrollDown(): chatIn veils the entry at once and photographs it two frames
// later, once that scroll has landed. `host` is the ancestor the bubble rules are scoped to.
export const chatEnter = (el, host) => { chatIn(el, 1, 0, { host }); return el; };


// lib/rasterize.js handles SVG (createImageBitmap rejects image/svg+xml); `width`/`height` are
// the scan entry's dims, for sources declaring none.
export const toLlmImage = async (source) => {
  const img = splitDataUrl(await rasterizeToPngDataUrl(source, { maxEdge: MAX_IMAGE_EDGE }));
  if (!img) throw new Error('could not encode the image');
  return img;
};

export const entryName = (entry) => (entry && entry.name) || 'image';
