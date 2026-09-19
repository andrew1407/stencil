import LAYOUT_FIELDS_DATA from '../config/layoutFields.json' with { type: 'json' };

// Pure layout helpers: serialization, validation, geometry-edit indices. No DOM/app state.
// LAYOUT_FIELDS (config/layoutFields.json, canonical for every surface): array order IS the
// byte order of the session payload; the `export` index IS the byte order of the export/server
// subset; `exportForce` emits even when undefined, else only when `!= null`.
export const LAYOUT_FIELDS = LAYOUT_FIELDS_DATA;

const EXPORT_FIELDS = LAYOUT_FIELDS
  .filter(f => f.export != null)
  .sort((a, b) => a.export - b.export);

// The caller resolves DOM-derived values (zoom/scroll) first; every table field, table order.
export const serializeSession = (state) => {
  const out = {};
  for (const f of LAYOUT_FIELDS) out[f.key] = state[f.key];
  return out;
};

// cropRect: the canonical wire spelling is {x,y,w,h} on every surface; legacy payloads spell
// it {x,y,width,height}. Readers accept both, canonical wins.

// → canonical {x,y,w,h} for emission.
const canonicalCropRect = (r) => {
  if (!r || typeof r !== 'object') return r;
  return { x: r.x, y: r.y, w: r.w ?? r.width, h: r.h ?? r.height };
};

// → the app's internal {x,y,width,height}; null for non-objects.
export const normalizeCropRect = (r) => {
  if (!r || typeof r !== 'object') return null;
  return { x: r.x, y: r.y, width: r.w ?? r.width, height: r.h ?? r.height };
};

// The `export`-tagged subset in its own key order; `lines` by reference and optional fields
// omitted, so the JSON.stringify bytes stay stable.
export const buildLayoutPayload = (src) => {
  const out = {};
  for (const f of EXPORT_FIELDS) {
    const v = f.key === 'cropRect' ? canonicalCropRect(src[f.key]) : src[f.key];
    if (f.exportForce || v != null) out[f.key] = v;
  }
  return out;
};

// Order-independent dedupe key (fixed field order; matches desktop lineKey).
const lineDedupeKey = (l) => {
  if (!l || typeof l !== 'object') return JSON.stringify(l);
  const pts = Array.isArray(l.points) ? l.points.map((p) => `${p && p.x},${p && p.y}`).join(';') : '';
  return [l.color, l.pointColor ?? '', l.thickness, l.pointSize, l.style, l.locked ? 1 : 0, l.fillColor, pts].join('|');
};

// Union-merge for co-edit conflicts: server lines first, then any local line not present.
export const mergeLines = (serverLines, localLines) => {
  const out = Array.isArray(serverLines) ? serverLines.slice() : [];
  const seen = new Set(out.map(lineDedupeKey));
  for (const l of Array.isArray(localLines) ? localLines : []) {
    const k = lineDedupeKey(l);
    if (!seen.has(k)) { out.push(l); seen.add(k); }
  }
  return out;
};

// Bounded so a hostile #stencil= fragment / pasted JSON / co-edit payload cannot DoS the renderer.
const MAX_LINES = 50_000;
const MAX_POINTS_PER_LINE = 100_000;

const sanitizePoints = (pts) => {
  if (!Array.isArray(pts)) return [];
  const out = [];
  for (const p of pts) {
    if (out.length >= MAX_POINTS_PER_LINE) break;
    if (!p || typeof p !== 'object') continue;
    const x = Number(p.x), y = Number(p.y);
    if (Number.isFinite(x) && Number.isFinite(y)) out.push({ x, y });
  }
  return out;
};

// Whitelist copy onto a fresh object: the anti-prototype-pollution measure.
const sanitizeLine = (l) => {
  if (!l || typeof l !== 'object') return null;
  const line = { points: sanitizePoints(l.points) };
  if (typeof l.color === 'string') line.color = l.color;
  if (typeof l.pointColor === 'string') line.pointColor = l.pointColor;
  if (typeof l.fillColor === 'string') line.fillColor = l.fillColor;
  if (typeof l.style === 'string') line.style = l.style;
  const thickness = Number(l.thickness);
  if (Number.isFinite(thickness)) line.thickness = thickness;
  const pointSize = Number(l.pointSize);
  if (Number.isFinite(pointSize)) line.pointSize = pointSize;
  line.locked = !!l.locked;
  return line;
};

// Untrusted `lines`: each element rebuilt via the whitelist, capped at MAX_LINES.
export const sanitizeLines = (rawLines) => {
  if (!Array.isArray(rawLines)) return [];
  const out = [];
  for (const l of rawLines) {
    if (out.length >= MAX_LINES) break;
    const s = sanitizeLine(l);
    if (s) out.push(s);
  }
  return out;
};

// The shared ingress for uploads, pastes, #stencil= fragments and co-edit, so `lines` are
// sanitized here. The caller reads needsReplaceConfirm then needsDimMismatchConfirm.
export const validateLayout = (data, { hasImage, imgW, imgH, hasExistingLines }) => {
  if (!hasImage) return { ok: false, reason: 'no-image', needsReplaceConfirm: false, needsDimMismatchConfirm: false, lines: [] };
  const d = data && typeof data === 'object' ? data : {};
  return {
    ok: true,
    needsReplaceConfirm: !!hasExistingLines,
    needsDimMismatchConfirm: d.imageWidth !== imgW || d.imageHeight !== imgH,
    lines: sanitizeLines(d.lines)
  };
};

// Just after the focused point when it belongs to the shown line, else append.
export const resolveInsertIdx = (line, { coordLineIdx, selectedLineIdx, focusedPtIdx }) =>
  (coordLineIdx === selectedLineIdx && focusedPtIdx >= 0)
    ? focusedPtIdx + 1
    : line.points.length;

// The page (cm) at `dpi`, at least 1px per side. Mirrored by core::defaultBlankSizePx.
export const defaultBlankSizePx = ({ width, height }, dpi = 96) => {
  const toPx = cm => Math.max(1, Math.round(cm / 2.54 * dpi));
  return { width: toPx(width), height: toPx(height) };
};

export const fillState = (line, defaultFillColor) => {
  const enabled = !!(line.fillColor && line.fillColor !== 'transparent');
  return { enabled, value: enabled ? line.fillColor : (defaultFillColor || '#ffffff') };
};
