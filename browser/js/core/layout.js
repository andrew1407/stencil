// ── Pure layout helpers: serialization, validation, geometry-edit indices ──
// Extracted from DrawingApp so decision logic is unit-testable in Node WITHOUT a DOM.
// Never touch DOM/app state/globals — callers pass plain data in, act on returned descriptors.

// Build the layout export payload. `lines` passed by reference (no copy) so JSON.stringify
// output stays byte-identical to the old inline literals in downloadJSON/copyLayoutToClipboard.
// Optional fields are omitted when absent (file-export bytes unchanged); saveToServer passes
// filter/geometry + page format + formulas so they round-trip to peers and on reopen.
export const buildLayoutPayload = ({ imageWidth, imageHeight, lines, imageFilter, filterColor, cropRect, rotationQuarters, pageSize, customPageWidth, customPageHeight, allowFormulas, formulaX, formulaY }) => {
  const out = { imageWidth, imageHeight, lines };
  if (imageFilter != null) out.imageFilter = imageFilter;
  if (filterColor != null) out.filterColor = filterColor;
  if (cropRect != null) out.cropRect = cropRect;
  if (rotationQuarters != null) out.rotationQuarters = rotationQuarters;
  if (pageSize != null) out.pageSize = pageSize;
  if (customPageWidth != null) out.customPageWidth = customPageWidth;
  if (customPageHeight != null) out.customPageHeight = customPageHeight;
  if (allowFormulas != null) out.allowFormulas = allowFormulas;
  if (formulaX != null) out.formulaX = formulaX;
  if (formulaY != null) out.formulaY = formulaY;
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

// Decide what to do with an incoming layout object (upload or paste). Pure: DOM/confirm()/
// saveHistory/redraw stay in the calling method, which reads needsReplaceConfirm then
// needsDimMismatchConfirm (in that order) and the resolved `lines` array.
export const validateLayout = (data, { hasImage, imgW, imgH, hasExistingLines }) => {
  if (!hasImage) return { ok: false, reason: 'no-image', needsReplaceConfirm: false, needsDimMismatchConfirm: false, lines: [] };
  return {
    ok: true,
    needsReplaceConfirm: !!hasExistingLines,
    needsDimMismatchConfirm: data.imageWidth !== imgW || data.imageHeight !== imgH,
    lines: data.lines || []
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
