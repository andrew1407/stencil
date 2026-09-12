// ── Shared assistant helpers ────────────────────────────────────────────────
// The transcript's arrive/leave gestures and the two small entry adapters, used by
// assistant.js and the modules beside it.
import { chatIn, leaveThenRemove, CHAT_LEAVE_MS, scatterGridFor } from '../../lib/motion.js';
import { rasterizeToPngDataUrl } from '../../lib/rasterize.js';
import { splitDataUrl, MAX_IMAGE_EDGE } from '../../llm/chatController.js';

// Every assistant entry leaves on the same dissolve. `count` is how many are going at
// once — one removal gets the full fine mesh, a whole-transcript clear coarsens so the
// total number of flying tiles stays inside the budget (lib/motion.js scatterGridFor).
export const chatLeave = (el, done, count = 1, index = 0) =>
  leaveThenRemove(el, done, { ms: CHAT_LEAVE_MS, ...scatterGridFor(count, index) });
// …and every appended entry arrives on its mirror (lib/motion.js chatIn): the motes
// gather INTO it and it appears as they land. Called AFTER scrollDown() — chatIn veils
// the entry at once (it keeps its height, so the transcript scrolls to it as usual) and
// photographs it two frames later, once that scroll has landed; measured earlier the
// cloud is stranded above the entry or drawn over the composer. `host` is the section,
// the ancestor the bubble rules are scoped to.
export const chatEnter = (el, host) => { chatIn(el, 1, 0, { host }); return el; };


// Downscale + re-encode ANY image source as PNG → the LlmImage { mediaType, data }.
// lib/rasterize.js handles SVG (createImageBitmap rejects image/svg+xml) via an <img> +
// canvas fallback; `width`/`height` are the scan entry's dims, for sources declaring none.
export const toLlmImage = async (source) => {
  const img = splitDataUrl(await rasterizeToPngDataUrl(source, { maxEdge: MAX_IMAGE_EDGE }));
  if (!img) throw new Error('could not encode the image');
  return img;
};

export const entryName = (entry) => (entry && entry.name) || 'image';
