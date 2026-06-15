// ── Image-list filtering (pure, dependency-free, unit-tested) ────────────────
// The popup keeps the full scan result and derives the visible subset from a
// filter state. Kept here, free of chrome/DOM, so it runs under `node --test`.

// Bucket label for images whose format can't be detected (no extension, opaque
// URL, etc.). Surfaced as its own "ETC" checkbox so these can be filtered too.
export const UNKNOWN_FORMAT = 'etc';

// Lowercase image "format" from a URL or data: URI ('' if unknown).
export const formatOf = (src) => {
  if (!src) return '';
  if (src.startsWith('data:')) {
    const m = /^data:image\/([a-z0-9.+-]+)/i.exec(src);
    return m ? norm(m[1]) : '';
  }
  let path = src;
  try { path = new URL(src, 'http://_/').pathname; } catch { /* keep raw */ }
  const m = /\.([a-z0-9]{2,5})(?:[?#]|$)/i.exec(path);
  return m ? norm(m[1]) : '';
};

const norm = ext => ext.toLowerCase().replace('jpeg', 'jpg').replace('svg+xml', 'svg');

// Distinct, sorted formats present in the items.
export const distinctFormats = (items) => {
  const set = new Set();
  for (const it of items) {
    const f = formatOf(it.src);
    if (f) set.add(f);
  }
  return [...set].sort();
};

// Extract every url(...) target from a CSS background(-image) value.
export const extractCssUrls = (bg) => {
  const out = [];
  if (!bg || bg === 'none') return out;
  const re = /url\((['"]?)(.*?)\1\)/g;
  let m;
  while ((m = re.exec(bg))) if (m[2]) out.push(m[2]);
  return out;
};

// Does an item pass the active filter state?
//   f = { search, formats, minW, maxW, minH, maxH, includeImg, includeBg, includeVideo }
// `formats` is an array of allowed lowercase formats (from the checkbox group).
// An item's format must be in it; items whose format can't be detected are
// bucketed as UNKNOWN_FORMAT ('etc') and pass only when 'etc' is in the set.
// Numeric bounds are null/undefined when empty (= no bound). Items with unknown
// size (w/h <= 0) PASS the size filters — they're measured lazily afterwards.
export const passesFilters = (item, f = {}) => {
  if (item.kind === 'img' && f.includeImg === false) return false;
  if (item.kind === 'bg' && f.includeBg === false) return false;
  if (item.kind === 'video' && f.includeVideo === false) return false;

  if (f.search) {
    const q = f.search.toLowerCase();
    const inName = (item.name || '').toLowerCase().includes(q);
    const inSrc = (item.src || '').toLowerCase().includes(q);
    const inVideoUrl = (item.videoUrl || '').toLowerCase().includes(q);
    if (!inName && !inSrc && !inVideoUrl) return false;
  }

  // Videos are governed by the dedicated 'video' toggle, not the image-format set
  // (their still is an opaque JPEG frame), so don't format-filter them away.
  if (Array.isArray(f.formats) && item.kind !== 'video') {
    const fmt = formatOf(item.src) || UNKNOWN_FORMAT;
    if (!f.formats.includes(fmt)) return false;
  }

  if (item.w > 0) {
    if (isNum(f.minW) && item.w < f.minW) return false;
    if (isNum(f.maxW) && item.w > f.maxW) return false;
  }
  if (item.h > 0) {
    if (isNum(f.minH) && item.h < f.minH) return false;
    if (isNum(f.maxH) && item.h > f.maxH) return false;
  }
  return true;
};

const isNum = v => typeof v === 'number' && !isNaN(v);
