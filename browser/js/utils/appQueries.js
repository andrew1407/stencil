// True while a split compare view (vertical/horizontal) is actually showing. Shared by
// exportService/contextMenu/exportOptionsMenu/controlsBinder so the check can't drift.
export const isSplitCompare = (app) => app.compareMode === 'vertical' || app.compareMode === 'horizontal';

// Gates for the "Filter Only"/"Current" export-variant rows (contextMenu.js,
// exportOptionsMenu.js) — each would otherwise render byte-identical to a sibling variant.
export const hasActiveFilter = (app) => !!(app.imageFilter && app.imageFilter !== 'none');
export const hasAnyLines = (app) => !!(app.lines && app.lines.length > 0);

// ── Comparison view: is an image point in the EDITED half? ───────────────────
// Same geometry as the desktop's hover gate (mainWindow) — the two surfaces must agree
// point for point. `mode` is the EFFECTIVE mode (renderer.effectiveCompareMode), so the
// Alt+Shift+O peek is already folded in; callers pass the POINT's coords, not the cursor's.
export const compareEditedShows = (mode, split, x, y, imageW, imageH) => {
  if (mode === 'original') return false;
  if (mode !== 'vertical' && mode !== 'horizontal') return true;
  const f = Math.min(1, Math.max(0, Number.isFinite(split) ? split : 0.5));
  return mode === 'vertical' ? x >= imageW * f : y >= imageH * f;
};

const IMPERIAL_REGIONS = new Set(['US', 'LR', 'MM']);
// Seed the initial display unit from locale (a saved/typed preference overrides).
// No "measurement system" web API exists, so the region is derived via Intl.Locale
// (maximize() resolves bare "en" → US); only US/Liberia/Myanmar get inches. Never throws.
export const defaultUnitFromLocale = (
  nav = (typeof globalThis !== 'undefined' ? globalThis.navigator : undefined),
) => {
  try {
    // No usable locale tag → fall back to metric (the international default),
    // not to a US-biased guess.
    const tag = (nav && nav.languages && nav.languages[0]) || (nav && nav.language) || '';
    if (!tag) return 'cm';
    const loc = new Intl.Locale(tag);
    const region = (loc.region || loc.maximize().region || '').toUpperCase();
    return IMPERIAL_REGIONS.has(region) ? 'in' : 'cm';
  } catch {
    return 'cm';
  }
};
