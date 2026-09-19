// Heap marshalling for the wasm core's raw exports: flat point buffers, rect out-slots,
// C strings and the one reusable pixel scratch. Shared by the core*Ops modules beside it.
export const createMarshal = (core) => {
  const F64 = 8, I64 = 8, I32 = 4;

  // A CropRect {x,y,width,height} from a 4-double out pointer.
  const readRect = out => ({
    x: core.getValue(out, 'double'),
    y: core.getValue(out + F64, 'double'),
    width: core.getValue(out + 2 * F64, 'double'),
    height: core.getValue(out + 3 * F64, 'double')
  });
  const withRectOut = fill => {
    const out = core._malloc(4 * F64);
    try { fill(out); return readRect(out); } finally { core._free(out); }
  };

  // {x,y}[] → a malloc'd flat f64 buffer. Caller frees.
  const allocPoints = points => {
    const n = points.length;
    const ptr = core._malloc(n * 2 * F64);
    const view = new Float64Array(core.HEAPF64.buffer, ptr, n * 2);
    for (let i = 0; i < n; i++) {
      view[2 * i] = points[i].x;
      view[2 * i + 1] = points[i].y;
    }
    return { ptr, n, view };
  };

  // A NUL-terminated UTF-8 copy on the HEAP (no ~64KB stack limit). HEAPU8 is re-read after
  // _malloc since ALLOW_MEMORY_GROWTH can detach the old view.
  const utf8 = new TextEncoder();
  const withCString = (str, fn) => {
    const bytes = utf8.encode(str ?? '');
    const ptr = core._malloc(bytes.length + 1);
    try {
      core.HEAPU8.set(bytes, ptr);
      core.HEAPU8[ptr + bytes.length] = 0;
      return fn(ptr, bytes.length);
    } finally {
      core._free(ptr);
    }
  };

  // ONE image-sized scratch buffer, grown on demand: a _malloc/_free per filter call churned
  // the heap on every repaint. HEAPU8 is re-read at each use (memory growth detaches it).
  let scratch = { ptr: 0, bytes: 0 };
  const pixelScratch = (bytes) => {
    if (bytes > scratch.bytes) { if (scratch.ptr) core._free(scratch.ptr); scratch = { ptr: core._malloc(bytes), bytes }; }
    return scratch.ptr;
  };

  return { F64, I64, I32, withRectOut, allocPoints, withCString, pixelScratch };
};
