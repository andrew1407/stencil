// ── Pure layout helpers: serialization, validation, geometry-edit indices ──
// Extracted from DrawingApp so the decision logic is unit-testable in Node
// WITHOUT a DOM. These functions never touch the DOM, app state, or globals —
// callers pass plain data in and act on the returned descriptors.

// Build the layout export payload. `lines` is passed through by reference (no
// copy) so JSON.stringify output stays byte-identical to the previous inline
// object literals in downloadJSON / copyLayoutToClipboard.
export const buildLayoutPayload = ({ imageWidth, imageHeight, lines }) => ({
  imageWidth,
  imageHeight,
  lines
});

// Decide what to do with an incoming layout object (from upload or paste).
// Pure: the DOM/confirm()/saveHistory/redraw stay in the calling method, which
// reads needsReplaceConfirm / needsDimMismatchConfirm (in that order) and the
// resolved `lines` array.
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
