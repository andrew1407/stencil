// ── Popover placement (panel dialogs) ───────────────────────────────────────
// Below the anchor with left edges aligned, flipped above when the bottom would
// overflow, clamped inside the viewport on both axes. Port of browser/js/ui/popover.js
// popoverPosition — keep the two rule-for-rule (tests/popover.test.js carries the
// browser suite's placement cases).

/**
 * @param {object} args - `{anchor, box, viewport}` rects; `gap` is the anchor↔box space,
 *   `margin` the minimum distance to every viewport edge.
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
