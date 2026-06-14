import { core } from './stencilCore.js';
// ── Crop-window geometry ────────────────────────────────────────
// Port of desktop/core/cropGeometry.{hpp,cpp}. A crop is an axis-aligned
// rectangle {x, y, width, height} in ORIGINAL-image pixel space; the main canvas
// shows exactly that sub-rectangle and line/marker points live in crop-local
// pixels. The original image is never modified — only the rectangle is stored —
// so the crop can be moved, resized, or flipped (album↔portrait) losslessly.
//
// The crop aspect is fixed to the page (e.g. A3 = 42/29.7 ≈ √2), so resizing is
// corner-only. Each public function routes to the shared C++ core (wasm) when
// loaded, falling back to the JS reference below (which the wasm build mirrors —
// see tests/cropGeometry.test.js and tests/wasm-parity.test.js).

// ── JS reference implementations (exported for parity tests) ──

export const isAlbumOrientationJS = (width, height) => width > height;

export const cropAspectJS = (pageWidth, pageHeight, album) => {
  const lo = Math.min(pageWidth, pageHeight);
  const hi = Math.max(pageWidth, pageHeight);
  if (lo <= 0 || hi <= 0) return 1;
  return album ? hi / lo : lo / hi;
};

export const centeredCropJS = (imageW, imageH, aspectWoverH) => {
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

export const resizeCropFromCornerJS = (cur, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize = 16) => {
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

export const moveCropClampedJS = (cur, dx, dy, imageW, imageH) => {
  const maxX = Math.max(0, imageW - cur.width);
  const maxY = Math.max(0, imageH - cur.height);
  return {
    x: Math.min(Math.max(cur.x + dx, 0), maxX),
    y: Math.min(Math.max(cur.y + dy, 0), maxY),
    width: cur.width,
    height: cur.height
  };
};

export const cropResizeScaleJS = (oldWidth, newWidth) =>
  oldWidth > 0 ? newWidth / oldWidth : 1;

export const cropChangeJS = (oldRect, newRect) => {
  const orientationChanged =
    isAlbumOrientationJS(oldRect.width, oldRect.height) !==
    isAlbumOrientationJS(newRect.width, newRect.height);
  return {
    orientationChanged,
    scale: orientationChanged ? 1 : cropResizeScaleJS(oldRect.width, newRect.width)
  };
};

// Multiply every point of every line by `scale` in place (crop-local rescale).
export const scaleLinePoints = (lines, scale) => {
  for (const line of lines)
    for (const p of line.points) {
      p.x *= scale;
      p.y *= scale;
    }
};

// ── Public API: wasm when loaded, JS reference otherwise ──

export const isAlbumOrientation = (w, h) =>
  (core.op('isAlbumOrientation') ?? isAlbumOrientationJS)(w, h);

export const cropAspect = (pageWidth, pageHeight, album) =>
  (core.op('cropAspect') ?? cropAspectJS)(pageWidth, pageHeight, album);

export const centeredCrop = (imageW, imageH, aspectWoverH) =>
  (core.op('centeredCrop') ?? centeredCropJS)(imageW, imageH, aspectWoverH);

export const resizeCropFromCorner = (cur, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize = 16) =>
  (core.op('resizeCropFromCorner') ?? resizeCropFromCornerJS)(cur, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize);

export const moveCropClamped = (cur, dx, dy, imageW, imageH) =>
  (core.op('moveCropClamped') ?? moveCropClampedJS)(cur, dx, dy, imageW, imageH);

export const cropResizeScale = (oldWidth, newWidth) =>
  (core.op('cropResizeScale') ?? cropResizeScaleJS)(oldWidth, newWidth);

export const cropChange = (oldRect, newRect) =>
  (core.op('cropChange') ?? cropChangeJS)(oldRect, newRect);
