// ── Export-variant registry: ONE table for both menus ───────────────────────
// The context menu's Copy/Download Image flyouts (contextMenu.js) and the toolbar
// copy/download options lists (exportOptionsMenu.js) render the same rows — same
// labels, glyphs, order, and availability rules — from here, so they cannot drift.
import { isSplitCompare, hasActiveFilter, hasAnyLines } from '../utils.js';

// Row order: 'split' leads while a split compare view is active.
export const EXPORT_VARIANTS = ['split', 'current', 'original', 'tint'];
export const EXPORT_VARIANT_LABELS = {
  split:    'With Compare',
  current:  'Current (Tint + Lines/Points)',
  original: 'Original (No Tint, No Lines/Points)',
  tint:     'Filter Only (No Lines/Points)',
};
// A distinct glyph per variant; 'current' keeps whichever action icon the menu passes
// (copy/download), so that row reads as the trigger's own action.
export const EXPORT_VARIANT_ICONS = { split: 'compare', original: 'image', tint: 'palette' };

// Which variant rows exist right now, and which one owns the PRIMARY combo (Ctrl+C /
// Ctrl+Shift+D).
//
// "With Compare" is a FOURTH row, not a relabeling of 'current' — the two stay
// independent options: 'current' (tint + lines/points) is always reachable, comparing
// or not, and 'split' only appears (as the FIRST row) while a split compare view is
// active. The row that owns the primary combo is whichever of the two is relevant right
// now — 'split' while comparing, 'current' otherwise — so the chip moves between rows
// instead of sitting on both (desktop parity: syncSplitCopyDownloadSlot in
// mainWindow.cpp; user report — a broken earlier version replaced 'current' with
// 'split' instead of adding a row alongside it).
//
// 'tint' is offered only with a filter active ('Filter Only' would otherwise render
// byte-identical to 'Original'); 'current' only with something drawn (it would
// otherwise render identical to whichever of Filter Only/Original applies).
export const exportVariantState = (app) => {
  const hasImage = !!app?.image;
  const split = hasImage && isSplitCompare(app);
  return {
    primary: split ? 'split' : 'current',
    show: {
      split,
      current: hasImage && hasAnyLines(app),
      original: hasImage,
      tint: hasImage && hasActiveFilter(app),
    },
  };
};
