// Shared by exportService/contextMenu/exportOptionsMenu/controlsBinder so the check cannot drift.
export const isSplitCompare = (app) => app.compareMode === 'vertical' || app.compareMode === 'horizontal';

// Gates for the "Filter Only"/"Current" export-variant rows, which would otherwise
// render byte-identical to a sibling variant.
export const hasActiveFilter = (app) => !!(app.imageFilter && app.imageFilter !== 'none');
export const hasAnyLines = (app) => !!(app.lines && app.lines.length > 0);

// Same geometry as the desktop's hover gate (mainWindow). `mode` is the EFFECTIVE mode
// (renderer.effectiveCompareMode); callers pass the POINT's coords, not the cursor's.
export const compareEditedShows = (mode, split, x, y, imageW, imageH) => {
  if (mode === 'original') return false;
  if (mode !== 'vertical' && mode !== 'horizontal') return true;
  const f = Math.min(1, Math.max(0, Number.isFinite(split) ? split : 0.5));
  return mode === 'vertical' ? x >= imageW * f : y >= imageH * f;
};

const IMPERIAL_REGIONS = new Set(['US', 'LR', 'MM']);
// No "measurement system" web API exists: the region comes from Intl.Locale (maximize()
// resolves bare "en" → US); only US/Liberia/Myanmar get inches. Never throws.
export const defaultUnitFromLocale = (
  nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined),
) => {
  try {
// No usable locale tag → metric, not a US-biased guess.
    const tag = (nav && nav.languages && nav.languages[0]) || (nav && nav.language) || '';
    if (!tag) return 'cm';
    const loc = new Intl.Locale(tag);
    const region = (loc.region || loc.maximize().region || '').toUpperCase();
    return IMPERIAL_REGIONS.has(region) ? 'in' : 'cm';
  } catch {
    return 'cm';
  }
};
