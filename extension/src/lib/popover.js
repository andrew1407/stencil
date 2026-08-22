// ── Popover placement (panel dialogs) ───────────────────────────────────────
// Where an anchored panel dialog sits relative to the control that opened it: below the
// anchor with left edges aligned, flipped above when the bottom would overflow, clamped
// inside the viewport on both axes. This is a PORT of browser/js/ui/popover.js
// popoverPosition (the extension can't import from browser/, a separate subproject) —
// keep the two rule-for-rule; tests/popover.test.js carries the browser suite's
// placement cases so they can't drift apart.

/**
 * @param {object} args
 * @param {{left:number, top:number, bottom:number}} args.anchor - The trigger's rect.
 * @param {{width:number, height:number}} args.box - The dialog's measured size.
 * @param {{width:number, height:number}} args.viewport - The panel window's size.
 * @param {number} [args.gap=8] - Space between the anchor and the box.
 * @param {number} [args.margin=8] - Minimum distance to every viewport edge.
 * @returns {{left:number, top:number}} The box's fixed position.
 */
export const popoverPosition = ({ anchor, box, viewport, gap = 8, margin = 8 }) => {
  let top = anchor.bottom + gap;
  if (top + box.height > viewport.height - margin) {
    const above = anchor.top - gap - box.height;
    top = above >= margin ? above : Math.max(margin, viewport.height - margin - box.height);
  }
  const left = Math.max(margin, Math.min(anchor.left, viewport.width - margin - box.width));
  return { left, top };
};
