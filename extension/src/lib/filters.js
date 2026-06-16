// ── Image-list filtering (pure, dependency-free, unit-tested) ────────────────

// Bucket label for media whose format can't be detected (no extension, opaque URL).
export const UNKNOWN_FORMAT = 'etc';

// Video container formats offered in the filter. A video's format comes from its
// media URL (item.videoUrl), not its opaque JPEG still — see formatOfItem.
export const VIDEO_FORMATS = ['mp4', 'webm', 'mov', 'avi', 'mkv', 'm4v', 'ogv'];

// Lowercase media "format" from a URL or data: URI ('' if unknown).
export const formatOf = (src) => {
  if (!src) return '';
  if (src.startsWith('data:')) {
    const m = /^data:(?:image|video)\/([a-z0-9.+-]+)/i.exec(src);
    return m ? norm(m[1]) : '';
  }
  let path = src;
  try { path = new URL(src, 'http://_/').pathname; } catch { /* keep raw */ }
  const m = /\.([a-z0-9]{2,5})(?:[?#]|$)/i.exec(path);
  return m ? norm(m[1]) : '';
};

// The format used to filter an item: a video keys on its media URL, the rest on `src`.
export const formatOfItem = (item) =>
  item && item.kind === 'video' ? formatOf(item.videoUrl) : formatOf(item && item.src);

const norm = ext => ext.toLowerCase().replace('jpeg', 'jpg').replace('svg+xml', 'svg').replace('quicktime', 'mov');

// Distinct, sorted formats present in the items.
export const distinctFormats = (items) => {
  const set = new Set();
  for (const it of items) {
    const f = formatOfItem(it);
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
// Undetectable formats bucket as UNKNOWN_FORMAT ('etc'). Empty numeric bounds are
// null. Items with unknown size (w/h <= 0) pass the size filters (measured later).
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

  if (Array.isArray(f.formats)) {
    const fmt = formatOfItem(item) || UNKNOWN_FORMAT;
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
