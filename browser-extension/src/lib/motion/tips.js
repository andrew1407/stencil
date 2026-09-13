// ── A hover tip's clocks and the point its dust belongs to ──────────────────
// A hover TIP is brisker still: re-triggered mid-sweep, its flight must be over before the
// next begins. Names shared with browser motion.js so the ported modules import them as-is.
export const TIP_DUST_IN_MS = 260;
export const TIP_DUST_OUT_MS = 190;
// …and wakes on one delay across surfaces (desktop SnappyTooltipStyle, main.cpp).
export const TIP_SHOW_DELAY_MS = 200;

// The centre of an element (or rect): the point a popup's dust belongs to. Null for a
// detached owner — the flight then settles instead.
export const rectCenter = (elOrRect) => {
  const r = typeof elOrRect?.getBoundingClientRect === 'function'
    ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !(r.width > 0 || r.height > 0)) return null;
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
};
