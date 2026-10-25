// The core's crop ops over the wasm ABI: the page aspect, the centred default rect, every
// rect edit (corner resize, move, scale, orientation swap, quarter rotation, mirror) and the commit snap.
export const buildCropOps = (core, { withF64Out, withRectOut }) => {
  const cIsAlbum         = core.cwrap('stencil_isAlbumOrientation', 'number', ['number', 'number']);
  const cCropAspect      = core.cwrap('stencil_cropAspect', 'number', ['number', 'number', 'number']);
  const cCropResizeScale = core.cwrap('stencil_cropResizeScale', 'number', ['number', 'number']);
  const cCenteredCrop = core.cwrap('stencil_centeredCrop', null, ['number', 'number', 'number', 'number']);
  const cResizeCorner = core.cwrap('stencil_resizeCropFromCorner', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cMoveCrop = core.cwrap('stencil_moveCropClamped', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cScaleCrop = core.cwrap('stencil_scaleCropCentered', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cSwapCrop = core.cwrap('stencil_swapCropOrientation', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cCropChange = core.cwrap('stencil_cropChange', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cRotateCrop = core.cwrap('stencil_rotateCropRectQuarter', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cSnapCrop = core.cwrap('stencil_snapCropRect', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cTurnEdit = core.cwrap('stencil_rotateEditQuarter', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cMirrorEdit = core.cwrap('stencil_mirrorEdit', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']);

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
      const at = withF64Out(out => cCropChange(oldRect.x, oldRect.y, oldRect.width, oldRect.height, newRect.x, newRect.y, newRect.width, newRect.height, out));
      return { orientationChanged: at(0) === 1, scale: at(1) };
    },

    rotateCropRectQuarter(r, imageW, imageH, clockwise) {
      return withRectOut(out => cRotateCrop(r.x, r.y, r.width, r.height, imageW, imageH, clockwise ? 1 : 0, out));
    },

    snapCropRect(r, imageW, imageH) {
      return withRectOut(out => cSnapCrop(r.x, r.y, r.width, r.height, imageW, imageH, out));
    },

    // out[0..3] = the snapped window, out[4] = the wrapped quarter count.
    rotateEditQuarter(crop, quarters, originalW, originalH, clockwise) {
      const at = withF64Out(out => cTurnEdit(crop.x, crop.y, crop.width, crop.height, quarters, originalW, originalH, clockwise ? 1 : 0, out));
      return { crop: { x: at(0), y: at(1), width: at(2), height: at(3) }, quarters: at(4) };
    },

    // out[0..3] = the reflected window, out[4] = the negated quarter count.
    mirrorEdit(crop, quarters, originalW, originalH) {
      const at = withF64Out(out => cMirrorEdit(crop.x, crop.y, crop.width, crop.height, quarters, originalW, originalH, out));
      return { crop: { x: at(0), y: at(1), width: at(2), height: at(3) }, quarters: at(4) };
    },
  };
};
