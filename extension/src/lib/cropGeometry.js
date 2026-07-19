// ── Crop-window geometry (extension copy) ───────────────────────────────────
// Behaviour-identical port of browser/js/core/cropGeometry.js. A crop is an axis-aligned
// rect {x,y,width,height} in ORIGINAL-image pixels whose aspect is locked to the chosen
// page (resizing is corner-only). Keep in sync with the editor (tests/cropGeometry.test.js).

// Page natural dimensions (cm, portrait). Mirrors browser/js/config/constants.json:
// the full ISO 216 A/B + ISO 269 C series in canonical order (A0..A10, B0..B10, C0..C10).
export const PAGE_SIZES = {
  A0: { width: 84.1, height: 118.9 },
  A1: { width: 59.4, height: 84.1 },
  A2: { width: 42, height: 59.4 },
  A3: { width: 29.7, height: 42 },
  A4: { width: 21, height: 29.7 },
  A5: { width: 14.8, height: 21 },
  A6: { width: 10.5, height: 14.8 },
  A7: { width: 7.4, height: 10.5 },
  A8: { width: 5.2, height: 7.4 },
  A9: { width: 3.7, height: 5.2 },
  A10: { width: 2.6, height: 3.7 },
  B0: { width: 100, height: 141.4 },
  B1: { width: 70.7, height: 100 },
  B2: { width: 50, height: 70.7 },
  B3: { width: 35.3, height: 50 },
  B4: { width: 25, height: 35.3 },
  B5: { width: 17.6, height: 25 },
  B6: { width: 12.5, height: 17.6 },
  B7: { width: 8.8, height: 12.5 },
  B8: { width: 6.2, height: 8.8 },
  B9: { width: 4.4, height: 6.2 },
  B10: { width: 3.1, height: 4.4 },
  C0: { width: 91.7, height: 129.7 },
  C1: { width: 64.8, height: 91.7 },
  C2: { width: 45.8, height: 64.8 },
  C3: { width: 32.4, height: 45.8 },
  C4: { width: 22.9, height: 32.4 },
  C5: { width: 16.2, height: 22.9 },
  C6: { width: 11.4, height: 16.2 },
  C7: { width: 8.1, height: 11.4 },
  C8: { width: 5.7, height: 8.1 },
  C9: { width: 4, height: 5.7 },
  C10: { width: 2.8, height: 4 }
};
export const DEFAULT_PAGE = 'A3';

// Selector label for a named format, e.g. "A4 (21 × 29.7 cm)" (extension UI copy
// of the shared label shape; the table values need no rounding/trimming).
export const pageSizeLabel = (name) => {
  const d = PAGE_SIZES[name];
  return d ? `${name} (${d.width} × ${d.height} cm)` : name;
};

// <option> markup for every named format (canonical order, labelled via
// pageSizeLabel) — shared by the options page and the crop dialog; callers
// prepend extras such as the crop dialog's Custom… entry.
export const pageSizeOptions = () =>
  Object.keys(PAGE_SIZES).map((n) => `<option value="${n}">${pageSizeLabel(n)}</option>`).join('');

export const isAlbumOrientation = (width, height) => width > height;

export const cropAspect = (pageWidth, pageHeight, album) => {
  const lo = Math.min(pageWidth, pageHeight);
  const hi = Math.max(pageWidth, pageHeight);
  if (lo <= 0 || hi <= 0) return 1;
  return album ? hi / lo : lo / hi;
};

export const centeredCrop = (imageW, imageH, aspectWoverH) => {
  if (imageW <= 0 || imageH <= 0 || aspectWoverH <= 0)
    return { x: 0, y: 0, width: 0, height: 0 };
  let w = imageW;
  let h = w / aspectWoverH;
  if (h > imageH) {
    h = imageH;
    w = h * aspectWoverH;
  }
  return { x: (imageW - w) / 2, y: (imageH - h) / 2, width: w, height: h };
};

/**
 * Resize an aspect-locked crop rect by dragging one corner; the opposite corner
 * stays anchored and the result is clamped inside the image.
 * @param {{x: number, y: number, width: number, height: number}} cur - Current crop rect.
 * @param {0|1|2|3} corner - Dragged corner: 0=TL, 1=TR, 2=BR, 3=BL.
 * @param {number} cursorX - Cursor x in original-image pixels.
 * @param {number} cursorY - Cursor y in original-image pixels.
 * @param {number} aspectWoverH - Locked width/height aspect ratio.
 * @param {number} imageW - Original image width (px).
 * @param {number} imageH - Original image height (px).
 * @param {number} [minSize=16] - Minimum crop width (px).
 * @returns {{x: number, y: number, width: number, height: number}} The resized rect.
 */
export const resizeCropFromCorner = (cur, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize = 16) => {
  if (aspectWoverH <= 0 || imageW <= 0 || imageH <= 0) return { ...cur };
  const movingLeft = corner === 0 || corner === 3;
  const movingTop = corner === 0 || corner === 1;
  const anchorX = movingLeft ? cur.x + cur.width : cur.x;
  const anchorY = movingTop ? cur.y + cur.height : cur.y;

  const dx = Math.max(0, movingLeft ? anchorX - cursorX : cursorX - anchorX);
  const dy = Math.max(0, movingTop ? anchorY - cursorY : cursorY - anchorY);
  let w = Math.max(dx, dy * aspectWoverH);

  const availW = movingLeft ? anchorX : imageW - anchorX;
  const availH = movingTop ? anchorY : imageH - anchorY;
  const maxW = Math.min(availW, availH * aspectWoverH);
  w = maxW < minSize ? maxW : Math.min(Math.max(w, minSize), maxW);
  const h = w / aspectWoverH;

  return {
    x: movingLeft ? anchorX - w : anchorX,
    y: movingTop ? anchorY - h : anchorY,
    width: w,
    height: h
  };
};

export const moveCropClamped = (cur, dx, dy, imageW, imageH) => {
  const maxX = Math.max(0, imageW - cur.width);
  const maxY = Math.max(0, imageH - cur.height);
  return {
    x: Math.min(Math.max(cur.x + dx, 0), maxX),
    y: Math.min(Math.max(cur.y + dy, 0), maxY),
    width: cur.width,
    height: cur.height
  };
};

// Scale a crop about its CENTRE by `factor` (>1 grows), aspect fixed, centre held (so growth
// is capped by the nearer edge), floored at `minSize`. Mirrors core/geometry/cropGeometry
// scaleCropCentered (and the editor's browser/js/core/cropGeometry.js) so the quick-crop
// wheel/pinch resize matches the full editor.
export const scaleCropCentered = (cur, factor, aspectWoverH, imageW, imageH, minSize = 16) => {
  if (factor <= 0 || cur.width <= 0 || cur.height <= 0 || aspectWoverH <= 0) return { ...cur };
  const cx = cur.x + cur.width * 0.5;
  const cy = cur.y + cur.height * 0.5;
  let w = cur.width * factor;
  let h = w / aspectWoverH;
  if (w < minSize) { w = minSize; h = w / aspectWoverH; }
  if (h < minSize) { h = minSize; w = h * aspectWoverH; }
  const maxHalfW = Math.min(cx, imageW - cx);
  const maxHalfH = Math.min(cy, imageH - cy);
  const wMax = Math.min(2 * maxHalfW, 2 * maxHalfH * aspectWoverH);
  if (wMax > 0 && w > wMax) { w = wMax; h = w / aspectWoverH; }
  let x = cx - w * 0.5;
  let y = cy - h * 0.5;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > imageW) x = imageW - w;
  if (y + h > imageH) y = imageH - h;
  return { x, y, width: w, height: h };
};

// Snap a crop rect to integer pixels, clamped inside the original image.
export const roundRect = (r, iw, ih) => {
  const w = Math.max(1, Math.min(Math.round(r.width), iw));
  const h = Math.max(1, Math.min(Math.round(r.height), ih));
  const x = Math.max(0, Math.min(Math.round(r.x), iw - w));
  const y = Math.max(0, Math.min(Math.round(r.y), ih - h));
  return { x, y, width: w, height: h };
};

// Resolve page dimensions (cm). `page` is any ISO format name ('A0'..'C10') or
// 'custom'; for 'custom' pass the explicit width/height (cm). Unknown names fall
// back to A4 (mirrors the editor).
export const pageDims = (page, customW, customH) => {
  if (page === 'custom') return { width: customW, height: customH };
  return PAGE_SIZES[page] || PAGE_SIZES.A4;
};
