// The core's line-list ops over the wasm ABI: the co-edit merge's keep mask (a runtime op), and
// the chain edits the app runs in JS on its live lines (parity only). Lines cross as the
// linesCodec.js pair, a point list as one flat [x0,y0,…] array.
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { encodeLines } from '../line/linesCodec.js';
import { heapAllocAll } from './coreMarshal.js';

const { layoutLinesMax, layoutLinePointsMax, layoutPointsMax } = constants.LIMITS;

// What decodeLines keeps whole: past a cap the core would key a cut line, so the mask declines.
const codecSafe = (lines) => {
  if (lines.length > layoutLinesMax) return false;
  let total = 0;
  for (const l of lines) {
    if (!l || typeof l !== 'object') return false;
    const pts = l.points ?? [];
    if (!Array.isArray(pts) || pts.length > layoutLinePointsMax) return false;
    if (pts.some((p) => !p || typeof p !== 'object')) return false;
    total += pts.length;
  }
  return total <= layoutPointsMax;
};

export const buildLineOps = (core, { F64, I32 }) => {
  const NUM = 'number';
  const cKeep = core.cwrap('stencil_mergeLinesKeep', NUM, [NUM, NUM, NUM, NUM, NUM, NUM, NUM, NUM, NUM, NUM]);
  const cUnchain = core.cwrap('stencil_chainUnchain', NUM, [NUM, NUM, NUM, NUM]);
  const cPull = core.cwrap('stencil_chainPullOut', NUM, [NUM, NUM, NUM, NUM, NUM, NUM, NUM, NUM, NUM]);

  // Every buffer _malloc'd for the call and freed after; a zero-length one still gets a byte.
  const withHeap = (bytes, use) => {
    const ptrs = heapAllocAll(core, bytes.map((n) => Math.max(1, n)));
    try { return use(ptrs); } finally { ptrs.forEach((p) => core._free(p)); }
  };
  const flat = (points) => points.flatMap((p) => [p.x, p.y]);
  const readPoints = (ptr, n) => {
    const v = new Float64Array(core.HEAPF64.buffer, ptr, 2 * n);
    return Array.from({ length: n }, (_, i) => ({ x: v[2 * i], y: v[2 * i + 1] }));
  };

  return {
    // null when the lists fall outside the codec's reach; layout.js then asks the JS reference.
    mergeLinesKeep(server, local) {
      if (!codecSafe(server) || !codecSafe(local)) return null;
      const s = encodeLines(server), l = encodeLines(local);
      return withHeap([s.nums.length * F64, s.text.length, l.nums.length * F64, l.text.length, local.length],
        ([sn, st, ln, lt, keep]) => {
          core.HEAPF64.set(s.nums, sn / F64);
          core.HEAPU8.set(s.text, st);
          core.HEAPF64.set(l.nums, ln / F64);
          core.HEAPU8.set(l.text, lt);
          const n = cKeep(sn, s.nums.length, st, s.text.length, ln, l.nums.length, lt, l.text.length, keep, local.length);
          if (n !== local.length) return null;
          return Array.from(core.HEAPU8.subarray(keep, keep + n), (b) => b === 1);
        });
    },

    // Mirrors dragGestures.unchainLine: the line is edited in place.
    unchainLine(line) {
      if (!line || !line.locked) return false;
      const pts = line.points;
      return withHeap([pts.length * 2 * F64, pts.length * 2 * F64], ([src, out]) => {
        core.HEAPF64.set(flat(pts), src / F64);
        const n = cUnchain(src, pts.length, 1, out);
        if (n < 0) return false;
        line.points = readPoints(out, n);
        line.locked = false;
        line.fillColor = 'transparent';
        return true;
      });
    },

    // Mirrors dragGestures.pullOutPoint: `target` is {kind, ptIdx, ptIdx2}.
    pullOutPoint(line, target, x, y) {
      if (!line || !target) return -1;
      const pts = line.points, onPoint = target.kind === 'point';
      const cap = (pts.length + 1) * 2 * F64;
      return withHeap([pts.length * 2 * F64, cap, I32], ([src, out, count]) => {
        core.HEAPF64.set(flat(pts), src / F64);
        const at = cPull(src, pts.length, line.locked ? 1 : 0, onPoint ? 1 : 0,
          onPoint ? target.ptIdx : target.ptIdx2, x, y, out, count);
        if (line.locked) {
          line.locked = false;
          line.fillColor = 'transparent';
        }
        line.points = readPoints(out, core.getValue(count, 'i32'));
        return at;
      });
    },
  };
};
