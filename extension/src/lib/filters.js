// The bucket for media whose format cannot be detected.
export const UNKNOWN_FORMAT = 'etc';

export const VIDEO_FORMATS = ['mp4', 'webm', 'mov', 'avi', 'mkv', 'm4v', 'ogv'];

export const formatOf = (src) => {
  if (!src) return '';
  if (src.startsWith('data:')) {
    const m = /^data:(?:image|video)\/([a-z0-9.+-]+)/i.exec(src);
    return m ? norm(m[1]) : '';
  }
  let path = src;
  try {
    path = new URL(src, 'http://_/').pathname;
  } catch {
    /* keep raw */
  }
  const m = /\.([a-z0-9]{2,5})(?:[?#]|$)/i.exec(path);
  return m ? norm(m[1]) : '';
};

// A video's format comes from its media URL, not its opaque JPEG still.
export const formatOfItem = (item) =>
  item && item.kind === 'video' ? formatOf(item.videoUrl) : formatOf(item && item.src);

const norm = ext => ext.toLowerCase().replace('jpeg', 'jpg').replace('svg+xml', 'svg').replace('quicktime', 'mov');

export const distinctFormats = (items) => {
  const set = new Set();
  for (const it of items) {
    const f = formatOfItem(it);
    if (f) set.add(f);
  }
  return [...set].sort();
};

export const extractCssUrls = (bg) => {
  const out = [];
  if (!bg || bg === 'none') return out;
  const re = /url\((['"]?)(.*?)\1\)/g;
  let m;
  while ((m = re.exec(bg))) if (m[2]) out.push(m[2]);
  return out;
};

// An invalid regex matches nothing. Mirrored in pageApiMain.js and by the CLI/pystencil
// --source-name scrape filter.
export const matchesSearch = (item, f = {}) => {
  const query = f.search;
  if (!query) return true;
  const fields = [item.name, item.src, item.videoUrl];
  if (f.regex) {
    let re;
    try { re = new RegExp(query, 'i'); } catch { return false; }
    return fields.some(v => re.test(v || ''));
  }
  const q = query.toLowerCase();
  return fields.some(v => (v || '').toLowerCase().includes(q));
};

// Unknown-size items (w/h <= 0) pass the size filters — they are measured later.
export const passesFilters = (item, f = {}) => {
  // Posters and metadata images list as <img> but each has its own toggle.
  if (item.poster) {
    if (f.includePosters === false) return false;
  } else if (item.meta) {
    if (f.includeMeta === false) return false;
  } else {
    if (item.kind === 'img' && f.includeImg === false) return false;
    if (item.kind === 'bg' && f.includeBg === false) return false;
    if (item.kind === 'video' && f.includeVideo === false) return false;
  }

  if (f.search && !matchesSearch(item, f)) return false;

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
