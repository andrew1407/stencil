// The core's pixel ops over the wasm ABI: the RGBA filter table and the contour pass, both
// copying through the shared pixel scratch and writing the result back in place — or, with
// `inHeap`, handing back a view of the scratch, valid until the next core call.
// Must match core/raster/imageFilter.hpp.
const FILTER_MODE = { none: 0, bw: 1, sepia: 2, custom: 3, invert: 4, contour: 5 };

export const buildImageOps = (core, { pixelScratch, releasePixelScratch }) => {
  const cFilter  = (mode, ptr, n, r, g, b) =>
    core.ccall('stencil_applyFilterRGBA', null, ['number', 'number', 'number', 'number', 'number', 'number'], [mode, ptr, n, r, g, b]);
  const cContour = (ptr, w, h) => core.ccall('stencil_applyContourRGBA', null, ['number', 'number', 'number'], [ptr, w, h]);

  return {
    releasePixelScratch,

    applyFilterRGBA(mode, data, pixelCount, r, g, b, inHeap = false) {
      const bytes = pixelCount * 4;
      const ptr = pixelScratch(bytes);
      core.HEAPU8.set(data, ptr);
      cFilter(FILTER_MODE[mode] ?? FILTER_MODE.custom, ptr, pixelCount, r, g, b);
      if (inHeap) return new Uint8ClampedArray(core.HEAPU8.buffer, ptr, bytes);
      data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
    },

    // Contour needs the pixel neighborhood, so it crosses with width/height.
    applyContourRGBA(data, width, height, inHeap = false) {
      const bytes = width * height * 4;
      const ptr = pixelScratch(bytes);
      core.HEAPU8.set(data, ptr);
      cContour(ptr, width, height);
      if (inHeap) return new Uint8ClampedArray(core.HEAPU8.buffer, ptr, bytes);
      data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
    },
  };
};
