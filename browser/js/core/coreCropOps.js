// The core's crop ops over the wasm ABI: the page aspect, the centred default rect, and
// every rect edit (corner resize, move, scale, orientation swap, quarter rotation).
export const buildCropOps = (core, { F64, withRectOut }) => {
  const cIsAlbum         = core.cwrap('stencil_isAlbumOrientation', 'number', ['number', 'number']);
  const cCropAspect      = core.cwrap('stencil_cropAspect', 'number', ['number', 'number', 'number']);
  const cCropResizeScale = core.cwrap('stencil_cropResizeScale', 'number', ['number', 'number']);
  const cCenteredCrop = (iw, ih, aspect, out) =>
    core.ccall('stencil_centeredCrop', null, ['number', 'number', 'number', 'number'], [iw, ih, aspect, out]);
  const cResizeCorner = (x, y, w, h, corner, cx, cy, aspect, iw, ih, minSize, out) =>
    core.ccall('stencil_resizeCropFromCorner', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, corner, cx, cy, aspect, iw, ih, minSize, out]);
  const cMoveCrop   = (x, y, w, h, dx, dy, iw, ih, out) =>
    core.ccall('stencil_moveCropClamped', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, dx, dy, iw, ih, out]);
  const cScaleCrop  = (x, y, w, h, factor, aspect, iw, ih, out) =>
    core.ccall('stencil_scaleCropCentered', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, factor, aspect, iw, ih, out]);
  const cSwapCrop   = (x, y, w, h, aspect, iw, ih, out) =>
    core.ccall('stencil_swapCropOrientation', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, aspect, iw, ih, out]);
  const cCropChange = (ox, oy, ow, oh, nx, ny, nw, nh, out) =>
    core.ccall('stencil_cropChange', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [ox, oy, ow, oh, nx, ny, nw, nh, out]);
  const cRotateCrop = (x, y, w, h, iw, ih, cw, out) =>
    core.ccall('stencil_rotateCropRectQuarter', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, iw, ih, cw, out]);

  return {
    isAlbumOrientation(w, h) {
      return cIsAlbum(w, h) === 1;
    },

    cropAspect(pageWidth, pageHeight, album) {
      return cCropAspect(pageWidth, pageHeight, album ? 1 : 0);
    },

    centeredCrop(imageW, imageH, aspectWoverH) {
      return withRectOut(out => cCenteredCrop(imageW, imageH, aspectWoverH, out));
    },

    resizeCropFromCorner(cur, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize = 16) {
      return withRectOut(out => cResizeCorner(cur.x, cur.y, cur.width, cur.height, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize, out));
    },

    moveCropClamped(cur, dx, dy, imageW, imageH) {
      return withRectOut(out => cMoveCrop(cur.x, cur.y, cur.width, cur.height, dx, dy, imageW, imageH, out));
    },

    scaleCropCentered(cur, factor, aspectWoverH, imageW, imageH) {
      return withRectOut(out => cScaleCrop(cur.x, cur.y, cur.width, cur.height, factor, aspectWoverH, imageW, imageH, out));
    },

    swapCropOrientation(cur, aspectWoverH, imageW, imageH) {
      return withRectOut(out => cSwapCrop(cur.x, cur.y, cur.width, cur.height, aspectWoverH, imageW, imageH, out));
    },

    cropResizeScale(oldWidth, newWidth) {
      return cCropResizeScale(oldWidth, newWidth);
    },

    cropChange(oldRect, newRect) {
      const out = core._malloc(2 * F64);
      try {
        cCropChange(oldRect.x, oldRect.y, oldRect.width, oldRect.height, newRect.x, newRect.y, newRect.width, newRect.height, out);
        return { orientationChanged: core.getValue(out, 'double') === 1, scale: core.getValue(out + F64, 'double') };
      } finally {
        core._free(out);
      }
    },

    rotateCropRectQuarter(r, imageW, imageH, clockwise) {
      return withRectOut(out => cRotateCrop(r.x, r.y, r.width, r.height, imageW, imageH, clockwise ? 1 : 0, out));
    },
  };
};
