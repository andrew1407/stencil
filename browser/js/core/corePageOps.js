// The core's page-metric and point-geometry ops over the wasm ABI: page dimensions and the
// format table, the pixel→page mapping, and the in-place rotate / flip / bounding-box centre.
export const buildPageOps = (core, { F64, allocPoints }) => {
  const cPageFormats = core.cwrap('stencil_pageFormats', 'string', []);
  const cPageDims   = (name, cw, ch, cuW, cuH, out) =>
    core.ccall('stencil_pageDimensions', null, ['string', 'number', 'number', 'number', 'number', 'number', 'number'], [name, cw, ch, cuW, cuH, out, out + F64]);
  const cPixelRaw   = (x, y, dW, dH, cw, ch, out) =>
    core.ccall('stencil_pixelToPageRaw', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, dW, dH, cw, ch, out, out + F64]);
  const cRotate     = (ptr, n, cx, cy, ang) =>
    core.ccall('stencil_rotatePoints', null, ['number', 'number', 'number', 'number', 'number'], [ptr, n, cx, cy, ang]);
  const cFlip       = (ptr, n, horizontal, cx, cy) =>
    core.ccall('stencil_flipPoints', null, ['number', 'number', 'number', 'number', 'number'], [ptr, n, horizontal, cx, cy]);
  const cBboxCenter = (ptr, n, out) =>
    core.ccall('stencil_boundingBoxCenter', null, ['number', 'number', 'number'], [ptr, n, out]);

  // HEAPF64 may have detached if memory grew; re-view before reading back.
  const readPointsBack = (points, ptr, n) => {
    const back = new Float64Array(core.HEAPF64.buffer, ptr, n * 2);
    for (let i = 0; i < n; i++) {
      points[i].x = back[2 * i];
      points[i].y = back[2 * i + 1];
    }
  };

  return {
    pageDimensions(name, cw, ch, customW, customH) {
      const out = core._malloc(2 * F64);
      try {
        cPageDims(name, cw, ch, customW, customH, out);
        return { width: core.getValue(out, 'double'), height: core.getValue(out + F64, 'double') };
      } finally {
        core._free(out);
      }
    },

    // "A0 … C10" (no "custom") — the wasm twin of PAGE_SIZES in config/constants.json.
    pageFormats() {
      return cPageFormats();
    },

    pixelToPageRaw(x, y, dims, cw, ch) {
      const out = core._malloc(2 * F64);
      try {
        cPixelRaw(x, y, dims.width, dims.height, cw, ch, out);
        return { x: core.getValue(out, 'double'), y: core.getValue(out + F64, 'double') };
      } finally {
        core._free(out);
      }
    },

    rotatePoints(points, cx, cy, angle) {
      if (points.length === 0) return;
      const { ptr, n } = allocPoints(points);
      try {
        cRotate(ptr, n, cx, cy, angle);
        readPointsBack(points, ptr, n);
      } finally {
        core._free(ptr);
      }
    },

    // `horizontal` crosses the ABI as an int (1/0), like the other flag args.
    flipPoints(points, horizontal, cx, cy) {
      if (points.length === 0) return;
      const { ptr, n } = allocPoints(points);
      try {
        cFlip(ptr, n, horizontal ? 1 : 0, cx, cy);
        readPointsBack(points, ptr, n);
      } finally {
        core._free(ptr);
      }
    },

    boundingBoxCenter(points) {
      const { ptr, n } = allocPoints(points);
      const out = core._malloc(2 * F64);
      try {
        cBboxCenter(ptr, n, out);
        return { x: core.getValue(out, 'double'), y: core.getValue(out + F64, 'double') };
      } finally {
        core._free(ptr);
        core._free(out);
      }
    },
  };
};
