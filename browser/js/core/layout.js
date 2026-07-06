// ── Pure layout helpers: serialization, validation, geometry-edit indices ──
// Extracted from DrawingApp so decision logic is unit-testable in Node WITHOUT a DOM.
// Never touch DOM/app state/globals — callers pass plain data in, act on returned descriptors.

// ── The single persisted-field descriptor table ─────────────────────────────
// One source of truth for the two serializers below. Adding a persisted field is a
// ONE-line edit here: it flows into the full session layout (#buildLayout via
// serializeSession) automatically, and into the export/server subset when you tag it
// `export`. The array order IS the byte order of the full session payload; the `export`
// index (when set) IS the byte order of the export/server subset — the two paths emit
// different key orders on purpose, so both stay byte-identical to their old literals.
//   • export      — the 0-based position in the export/server subset (absent → session-only).
//   • exportForce — export the field unconditionally (undefined value included, matching the
//                   old `{ imageWidth, imageHeight, lines }` head); otherwise it's emitted
//                   only when `!= null`, matching the old `if (x != null)` guards.
export const LAYOUT_FIELDS = [
  { key: 'imageWidth', export: 0, exportForce: true },
  { key: 'imageHeight', export: 1, exportForce: true },
  { key: 'cropRect', export: 5 },
  { key: 'rotationQuarters', export: 6 },
  { key: 'lines', export: 2, exportForce: true },
  { key: 'pageSize', export: 7 },
  { key: 'customPageWidth', export: 8 },
  { key: 'customPageHeight', export: 9 },
  { key: 'unit' },
  { key: 'color' },
  { key: 'thickness' },
  { key: 'markerSize' },
  { key: 'style' },
  { key: 'showPoints' },
  { key: 'showLines' },
  { key: 'imageFilter', export: 3 },
  { key: 'filterColor', export: 4 },
  { key: 'zoom' },
  { key: 'scrollLeft' },
  { key: 'scrollTop' },
  { key: 'imageBaseName' },
  { key: 'imageExt' },
  { key: 'imageSource' },
  { key: 'imageResource' },
  { key: 'tooltipEnabled' },
  { key: 'tooltipShowPage' },
  { key: 'tooltipShowScreen' },
  { key: 'tooltipShowCoords' },
  { key: 'allowFormulas', export: 10 },
  { key: 'formulaX', export: 11 },
  { key: 'formulaY', export: 12 },
  { key: 'drawMode' },
  { key: 'holdDrawDelay' },
  { key: 'selGlowColor' },
  { key: 'hoverRingColor' },
  { key: 'focusRingColor' },
  { key: 'defaultFillColor' },
];

// Export-subset fields in their emitted (byte) order — derived once from the table.
const EXPORT_FIELDS = LAYOUT_FIELDS
  .filter(f => f.export != null)
  .sort((a, b) => a.export - b.export);

// Serialize the FULL session layout from a plain state object (no DOM, no DrawingApp —
// the caller resolves DOM-derived values like zoom/scroll first). Projects every table
// field in table order, so the produced payload is byte-identical to the old inline
// literal in Storage.#buildLayout. Pure: unit-testable with a plain object.
export const serializeSession = (state) => {
  const out = {};
  for (const f of LAYOUT_FIELDS) out[f.key] = state[f.key];
  return out;
};

// Build the layout export payload. `lines` passed by reference (no copy) so JSON.stringify
// output stays byte-identical to the old inline literals in downloadJSON/copyLayoutToClipboard.
// Optional fields are omitted when absent (file-export bytes unchanged); saveToServer passes
// filter/geometry + page format + formulas so they round-trip to peers and on reopen.
// Projects the `export`-tagged subset of LAYOUT_FIELDS in its own key order.
export const buildLayoutPayload = (src) => {
  const out = {};
  for (const f of EXPORT_FIELDS) {
    const v = src[f.key];
    if (f.exportForce || v != null) out[f.key] = v;
  }
  return out;
};

// Order-independent dedupe key for a line (fixed field order; matches desktop lineKey),
// so a local line dedupes against its server round-tripped twin. JSON-fallback for non-objects.
const lineDedupeKey = (l) => {
  if (!l || typeof l !== 'object') return JSON.stringify(l);
  const pts = Array.isArray(l.points) ? l.points.map((p) => `${p && p.x},${p && p.y}`).join(';') : '';
  return [l.color, l.thickness, l.markerSize, l.style, l.locked ? 1 : 0, l.fillColor, pts].join('|');
};

// Union-merge for co-edit conflicts: server lines first, then any local line not already
// present (order-independent) — keeps both editors' annotations without duplicating round-trips.
export const mergeLines = (serverLines, localLines) => {
  const out = Array.isArray(serverLines) ? serverLines.slice() : [];
  const seen = new Set(out.map(lineDedupeKey));
  for (const l of Array.isArray(localLines) ? localLines : []) {
    const k = lineDedupeKey(l);
    if (!seen.has(k)) { out.push(l); seen.add(k); }
  }
  return out;
};

// Ceilings for an ingested layout: generous enough for any real drawing, bounded so a
// hostile #stencil= fragment / pasted JSON / co-edit payload can't DoS the renderer with
// millions of lines or points.
const MAX_LINES = 50000;
const MAX_POINTS_PER_LINE = 100000;

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

// Rebuild a line from a whitelist of known fields onto a fresh plain object. This is
// the anti-prototype-pollution measure: __proto__/constructor/prototype (and any other
// injected key) simply never get copied, and every value is type-checked/coerced.
const sanitizeLine = (l) => {
  if (!l || typeof l !== 'object') return null;
  const line = { points: sanitizePoints(l.points) };
  if (typeof l.color === 'string') line.color = l.color;
  if (typeof l.fillColor === 'string') line.fillColor = l.fillColor;
  if (typeof l.style === 'string') line.style = l.style;
  const thickness = Number(l.thickness);
  if (Number.isFinite(thickness)) line.thickness = thickness;
  const markerSize = Number(l.markerSize);
  if (Number.isFinite(markerSize)) line.markerSize = markerSize;
  line.locked = !!l.locked;
  return line;
};

// Sanitize an untrusted `lines` array: non-array → empty; each element rebuilt via the
// whitelist above; capped at MAX_LINES. Exported so co-edit/merge paths can reuse it.
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

// Decide what to do with an incoming layout object (upload or paste). Pure: DOM/confirm()/
// saveHistory/redraw stay in the calling method, which reads needsReplaceConfirm then
// needsDimMismatchConfirm (in that order) and the resolved `lines` array. The `lines` are
// sanitized here (prototype-pollution keys stripped, sizes capped, coords validated) since
// this is the shared ingress for uploads, pasted layouts, #stencil= fragments, and co-edit.
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

// Where to splice a new point when extending a line: just after the focused
// point if the focused point belongs to the shown/selected line, else append.
export const resolveInsertIdx = (line, { coordLineIdx, selectedLineIdx, focusedPtIdx }) =>
  (coordLineIdx === selectedLineIdx && focusedPtIdx >= 0)
    ? focusedPtIdx + 1
    : line.points.length;

// Default pixel size for a generated blank image: the page (cm) rendered at
// `dpi` (CSS 96 by default), clamped to at least 1px per side. Mirrored by
// core::defaultBlankSizePx in core/pageMetrics.cpp.
export const defaultBlankSizePx = ({ width, height }, dpi = 96) => {
  const toPx = cm => Math.max(1, Math.round(cm / 2.54 * dpi));
  return { width: toPx(width), height: toPx(height) };
};

// Derive the selection-panel fill checkbox/color from a line's fillColor.
export const fillState = (line, defaultFillColor) => {
  const enabled = !!(line.fillColor && line.fillColor !== 'transparent');
  return { enabled, value: enabled ? line.fillColor : (defaultFillColor || '#3399ff') };
};
