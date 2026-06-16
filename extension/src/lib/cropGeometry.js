// ── Crop-window geometry (extension copy) ───────────────────────────────────
// Behaviour-identical port of browser/js/core/cropGeometry.js. A crop is an
// axis-aligned rect {x,y,width,height} in ORIGINAL-image pixels whose aspect is
// locked to the chosen page (so resizing is corner-only). Keep in sync with the
// editor (tests/cropGeometry.test.js).

// Page natural dimensions (cm). Mirrors browser/js/config/constants.json.
export const PAGE_SIZES = {
  A3: { width: 29.7, height: 42 },
  A4: { width: 21, height: 29.7 }
};
export const DEFAULT_PAGE = 'A3';

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

// corner: 0=TL, 1=TR, 2=BR, 3=BL
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

// Snap a crop rect to integer pixels, clamped inside the original image.
export const roundRect = (r, iw, ih) => {
  const w = Math.max(1, Math.min(Math.round(r.width), iw));
  const h = Math.max(1, Math.min(Math.round(r.height), ih));
  const x = Math.max(0, Math.min(Math.round(r.x), iw - w));
  const y = Math.max(0, Math.min(Math.round(r.y), ih - h));
  return { x, y, width: w, height: h };
};

// Resolve page dimensions (cm). `page` is 'A3' | 'A4' | 'custom'; for 'custom'
// pass the explicit width/height (cm).
export const pageDims = (page, customW, customH) => {
  if (page === 'custom') return { width: customW, height: customH };
  return PAGE_SIZES[page] || PAGE_SIZES.A4;
};
