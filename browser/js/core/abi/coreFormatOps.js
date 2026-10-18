// The core's text-formatting ops over the wasm ABI: the console logo banner rows.
import { heapAlloc } from './coreMarshal.js';

export const buildFormatOps = (core) => {
  const cBanner = core.cwrap('stencil_logoBanner', 'number', ['string', 'number', 'number', 'number', 'number']);
  const utf8 = new TextDecoder();
  return {
    // Sized first, then copied: the rows cross as one NUL-terminated string on the heap.
    logoBanner(mark, compact, boxed) {
      const needed = cBanner(String(mark), compact ? 1 : 0, boxed ? 1 : 0, 0, 0);
      if (!needed) return [];
      const ptr = heapAlloc(core, needed);
      try {
        cBanner(String(mark), compact ? 1 : 0, boxed ? 1 : 0, ptr, needed);
        return utf8.decode(core.HEAPU8.subarray(ptr, ptr + needed - 1)).split('\n');
      } finally {
        core._free(ptr);
      }
    },
  };
};
