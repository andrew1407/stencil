// One table for the context menu's Copy/Download Image flyouts and the toolbar options lists.
import { isSplitCompare, hasActiveFilter, hasAnyLines } from '../utils.js';

// Row order: 'split' leads while a split compare view is active.
export const EXPORT_VARIANTS = Object.freeze(['split', 'current', 'original', 'tint']);
export const EXPORT_VARIANT_LABELS = Object.freeze({
  split:    'With Compare',
  current:  'Current (Tint + Lines/Points)',
  original: 'Original (No Tint, No Lines/Points)',
  tint:     'Filter Only (No Lines/Points)',
});
// 'current' keeps whichever action icon the menu passes (copy/download).
export const EXPORT_VARIANT_ICONS = Object.freeze({ split: 'compare', original: 'image', tint: 'palette' });

// 'split' is first while a split compare is active and takes the primary combo from 'current'
// then (desktop: syncSplitCopyDownloadSlot). 'tint' needs a filter, 'current' something drawn.
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
