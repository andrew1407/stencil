import { core } from './stencilCore.js';
// ── Crop-window geometry ────────────────────────────────────────
// Port of core/cropGeometry.{hpp,cpp}. A crop is an axis-aligned rect {x,y,width,height} in
// ORIGINAL-image pixel space; the original is never modified (only the rect is stored), so
// moves/resizes/flips are lossless. Aspect is fixed to the page → corner-only resize. Public
// functions route to the wasm core when loaded, else the JS reference below (see parity tests).

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

// Rotate a crop rect one quarter turn within an image of imageW x imageH (the
// space the rect currently lives in). The turned image is imageH x imageW;
// `clockwise` rotates the picture right. Port of core::rotateCropRectQuarter.
export const rotateCropRectQuarterJS = (r, imageW, imageH, clockwise) =>
  clockwise
    ? { x: imageH - (r.y + r.height), y: r.x, width: r.height, height: r.width }
    : { x: r.y, y: imageW - (r.x + r.width), width: r.height, height: r.width };

// Rotate every crop-local point of every line one quarter turn inside a crop box
// of boxW x boxH, in place (the box becomes boxH x boxW). Like scaleLinePoints
// this runs in JS in both builds — no wasm marshalling needed.
export const rotateLinePointsQuarter = (lines, boxW, boxH, clockwise) => {
  for (const line of lines)
    for (const p of line.points) {
      const px = p.x;
      const py = p.y;
      if (clockwise) {
        p.x = boxH - py;
        p.y = px;
      } else {
        p.x = py;
        p.y = boxW - px;
      }
    }
};

// ── Public API: wasm when loaded, JS reference otherwise ──

export const isAlbumOrientation = core.bind('isAlbumOrientation', isAlbumOrientationJS);

export const cropAspect = core.bind('cropAspect', cropAspectJS);

export const centeredCrop = core.bind('centeredCrop', centeredCropJS);

export const resizeCropFromCorner = core.bind('resizeCropFromCorner', resizeCropFromCornerJS);

export const moveCropClamped = core.bind('moveCropClamped', moveCropClampedJS);

export const cropResizeScale = core.bind('cropResizeScale', cropResizeScaleJS);

export const cropChange = core.bind('cropChange', cropChangeJS);

export const rotateCropRectQuarter = core.bind('rotateCropRectQuarter', rotateCropRectQuarterJS);
