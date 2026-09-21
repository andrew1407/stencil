// Pure .stencil project-file (de)serializer: ORIGINAL image + export layout + metadata +
// optional theme.
import { buildLayoutPayload, sanitizeLines } from '../layout.js';
import { normalizeHex, isAccent } from '../settings/accents.js';

export const STENCIL_FILE_FORMAT = 'stencil-project';
export const STENCIL_FILE_VERSION = 1;

// chars ≈ bytes (ASCII-dominant JSON+base64); matches the server's 32 MiB MaxBodyBytes.
export const MAX_PROJECT_FILE_CHARS = 32 * 1024 * 1024;

const isPlainObject = (v) => !!v && typeof v === 'object' && !Array.isArray(v);

// { dataUrl (a `data:` URL), ext, w, h } → sanitized copy or null. Mirrors deepLink.normalizeLaunchPayload.
const sanitizeImage = (img) => {
  if (!isPlainObject(img)) return null;
  if (typeof img.dataUrl !== 'string' || !/^data:/i.test(img.dataUrl.trim())) return null;
  const out = { dataUrl: img.dataUrl };
  out.ext = (typeof img.ext === 'string' && img.ext.trim())
    ? img.ext.trim().toLowerCase().replace(/[^a-z0-9]/g, '').slice(0, 8) || 'png'
    : 'png';
  const w = Number(img.w), h = Number(img.h);
  if (Number.isFinite(w) && w > 0) out.w = Math.round(w);
  if (Number.isFinite(h) && h > 0) out.h = Math.round(h);
  return out;
};

// → { mode?, accent? } or null, so a themeless file never carries an empty {} reading as "has theme".
const sanitizeTheme = (t) => {
  if (!isPlainObject(t)) return null;
  const out = {};
  const mode = String(t.mode || '').toLowerCase();
  if (mode === 'dark' || mode === 'light') out.mode = mode;
  if (typeof t.accent === 'string') {
    if (isAccent(t.accent)) out.accent = t.accent;
    else { const hex = normalizeHex(t.accent); if (hex) out.accent = hex; }
  }
  return (out.mode || out.accent) ? out : null;
};

const cleanKeywords = (kw) => Array.isArray(kw)
  ? kw.filter((k) => typeof k === 'string' && k.trim()).map((k) => k.trim())
  : [];

// `state`: name, color, keywords[], source, resource, blank[Color], image, layout, theme.
export const buildProjectFile = (state = {}) => {
  const doc = {
    format: STENCIL_FILE_FORMAT,
    version: STENCIL_FILE_VERSION,
    name: (typeof state.name === 'string' && state.name.trim()) ? state.name.trim() : 'Untitled',
  };
  const color = normalizeHex(state.color);
  if (color) doc.color = color;
  const keywords = cleanKeywords(state.keywords);
  if (keywords.length) doc.keywords = keywords;
  if (typeof state.source === 'string' && state.source) doc.source = state.source;
  if (typeof state.resource === 'string' && state.resource) doc.resource = state.resource;
  if (state.blank) {
    doc.blank = true;
    const bc = normalizeHex(state.blankColor);
    if (bc) doc.blankColor = bc;
  }
  const image = sanitizeImage(state.image);
  if (image) doc.image = image;
// Through buildLayoutPayload so the byte shape matches the server/download layout exactly.
  doc.layout = buildLayoutPayload(isPlainObject(state.layout) ? state.layout : {});
  const theme = sanitizeTheme(state.theme);
  if (theme) doc.theme = theme;
  return doc;
};

export const serializeProjectFile = (state) => JSON.stringify(buildProjectFile(state), null, 2);

// → { ok:true, project } (the hardened shape for DrawingApp.applyProjectFile) or { ok:false, error }.
export const parseProjectFile = (input) => {
  if (typeof input === 'string' && input.length > MAX_PROJECT_FILE_CHARS) {
    return { ok: false, error: 'Project file is too large (over 32 MiB).' };
  }
  let data;
  try { data = typeof input === 'string' ? JSON.parse(input) : input; }
  catch (e) { return { ok: false, error: 'Not valid JSON: ' + e.message }; }
  if (!isPlainObject(data)) return { ok: false, error: 'Not a Stencil project file.' };
  if (data.format !== STENCIL_FILE_FORMAT) {
    return { ok: false, error: 'Not a Stencil project file (missing the "stencil-project" sentinel).' };
  }
  const version = Number(data.version);
  if (!Number.isFinite(version) || version < 1) {
    return { ok: false, error: 'Unrecognized project-file version.' };
  }
  if (version > STENCIL_FILE_VERSION) {
    return { ok: false, error: `This project needs a newer Stencil (file version ${version}).` };
  }

  const image = sanitizeImage(data.image);
  if (!image) return { ok: false, error: 'Project file has no embedded image.' };

  const layoutSrc = isPlainObject(data.layout) ? data.layout : {};
  const layout = buildLayoutPayload(layoutSrc);
  layout.lines = sanitizeLines(layoutSrc.lines);   // harden untrusted annotations

  const project = {
    name: (typeof data.name === 'string' && data.name.trim()) ? data.name.trim() : 'Untitled',
    color: normalizeHex(data.color) || '',
    keywords: cleanKeywords(data.keywords),
    source: typeof data.source === 'string' ? data.source : '',
    resource: typeof data.resource === 'string' ? data.resource : '',
    blank: !!data.blank,
    blankColor: normalizeHex(data.blankColor) || '',
    image,
    layout,
    theme: sanitizeTheme(data.theme),
  };
  return { ok: true, project };
};
