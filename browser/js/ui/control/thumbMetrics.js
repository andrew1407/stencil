// A scrollbar thumb's length and offset along its track. Pure arithmetic, shared by the canvas
// viewport's overlay bars (ui/scrollbars.js) and a capped menu's drawn one
// (ui/menuScrollbar.js). Byte-pinned to browser-extension/src/lib.
export const SB_MIN_THUMB_PX = 28;

// `track` px long, for a `client` window onto `scroll` px of content at `offset`.
// Null when nothing overflows — the caller draws no thumb at all.
export const thumbMetrics = (client, scroll, offset, track) => {
  if (!(scroll > client) || !(track > 0)) return null;
  const len = Math.min(track, Math.max(SB_MIN_THUMB_PX, Math.round(track * client / scroll)));
  const range = scroll - client;
  const at = Math.min(Math.max(offset, 0), range);
  return { len, pos: Math.round((track - len) * at / range) };
};
