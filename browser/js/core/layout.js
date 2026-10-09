import LAYOUT_FIELDS_DATA from '../../../common/config/layoutFields.json' with { type: 'json' };
import constants from '../../../common/config/constants.json' with { type: 'json' };
import { core } from './abi/stencilCore.js';
import { LINE_DEFAULTS } from './line/linesCodec.js';
import { storedSize } from './settings/limits.js';

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

// Fixed field order, a missing field as the core default, numbers as String(n) at full
// precision. Twin: core::lineDedupeKey (core/state/lineMerge).
export const lineDedupeKey = (l) => {
  const line = l && typeof l === 'object' ? l : {};
  const f = (k) => line[k] ?? LINE_DEFAULTS[k];
  const pts = Array.isArray(line.points) ? line.points.map((p) => `${Number(p?.x)},${Number(p?.y)}`).join(';') : '';
  return [String(f('color')), String(f('pointColor')), Number(f('thickness')), Number(f('pointSize')),
    String(f('style')), line.locked ? 1 : 0, String(f('fillColor')), pts].join('|');
};

// keep[i]: local line i joins the merge — no line before it, peer's or local, keys the same.
export const mergeKeepJS = (serverLines, localLines) => {
  const seen = new Set(serverLines.map(lineDedupeKey));
  return localLines.map((l) => {
    const k = lineDedupeKey(l);
    if (seen.has(k)) return false;
    seen.add(k);
    return true;
  });
};

// Union-merge for co-edit conflicts: server lines first, then any local line not present. The
// wasm mask declines (null) past the codec's caps, and the JS reference answers.
export const mergeLines = (serverLines, localLines) => {
  const server = Array.isArray(serverLines) ? serverLines : [];
  const local = Array.isArray(localLines) ? localLines : [];
  const keep = core.op('mergeLinesKeep')?.(server, local) || mergeKeepJS(server, local);
  return server.concat(local.filter((_, i) => keep[i]));
};

// Bounded so a hostile #stencil= fragment / pasted JSON / co-edit payload cannot DoS the renderer;
// the total also bounds the 64 history snapshots. Twin: the core's layout sanitiser.
const { layoutLinesMax: MAX_LINES, layoutLinePointsMax: MAX_POINTS_PER_LINE,
  layoutPointsMax: MAX_TOTAL_POINTS, lineNameMax: MAX_NAME } = constants.LIMITS;

const sanitizePoints = (pts, budget) => {
  if (!Array.isArray(pts)) return [];
  const cap = Math.min(MAX_POINTS_PER_LINE, budget);
  const out = [];
  for (const p of pts) {
    if (out.length >= cap) break;
    if (!p || typeof p !== 'object') continue;
    const x = Number(p.x), y = Number(p.y);
    if (Number.isFinite(x) && Number.isFinite(y)) out.push({ x, y });
  }
  return out;
};

// Whitelist copy onto a fresh object: the anti-prototype-pollution measure.
const sanitizeLine = (l, budget) => {
  if (!l || typeof l !== 'object') return null;
  const line = { points: sanitizePoints(l.points, budget) };
  if (typeof l.color === 'string') line.color = l.color;
  if (typeof l.pointColor === 'string') line.pointColor = l.pointColor;
  if (typeof l.fillColor === 'string') line.fillColor = l.fillColor;
  if (typeof l.style === 'string') line.style = l.style;
  const thickness = Number(l.thickness);
  if (Number.isFinite(thickness)) line.thickness = storedSize(thickness);
  const pointSize = Number(l.pointSize);
  if (Number.isFinite(pointSize)) line.pointSize = storedSize(pointSize);
  line.locked = !!l.locked;
  if (typeof l.name === 'string' && l.name) line.name = l.name.slice(0, MAX_NAME);
  if (l.hidden) line.hidden = true;
  return line;
};

// Untrusted `lines`: each element rebuilt via the whitelist. The line that spends the last of
// MAX_TOTAL_POINTS is cut there and every line after it is dropped.
export const sanitizeLines = (rawLines) => {
  if (!Array.isArray(rawLines)) return [];
  const out = [];
  let budget = MAX_TOTAL_POINTS;
  for (const l of rawLines) {
    if (out.length >= MAX_LINES || budget <= 0) break;
    const s = sanitizeLine(l, budget);
    if (s) { out.push(s); budget -= s.points.length; }
  }
  return out;
};

// Two capped lists joined (a combine, a co-edit union) can pass the caps: cut where they run out,
// as sanitizeLines does. Under them, the same array comes back.
export const capLayoutPoints = (lines) => {
  let budget = MAX_TOTAL_POINTS;
  for (let i = 0; i < lines.length; i++) {
    if (i >= MAX_LINES || budget <= 0) return lines.slice(0, i);
    const pts = Array.isArray(lines[i]?.points) ? lines[i].points : [];
    if (pts.length > budget) return [...lines.slice(0, i), { ...lines[i], points: pts.slice(0, budget) }];
    budget -= pts.length;
  }
  return lines;
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
