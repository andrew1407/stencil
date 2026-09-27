// Twin of core/abi/linesCodec.hpp, byte layout and all: a snapshot crosses the core ABI as
//   nums: [lineCount, then per line: pointCount, thickness, pointSize, locked,
//          byte lengths of color/style/fillColor/pointColor, then x0,y0,x1,y1,…]
//   text: those four strings per line, concatenated UTF-8, in that field order.
// A line missing a field encodes as the core's default for it (core/models.hpp).

import constants from '../../config/constants.json' with { type: 'json' };

const utf8 = new TextEncoder();
const utf8Decode = new TextDecoder();
// The layout caps sanitizeLines (layout.js) applies, so a decoded snapshot is bounded the same way.
const { layoutLinesMax: MAX_LINES, layoutLinePointsMax: MAX_LINE_POINTS,
  layoutPointsMax: MAX_POINTS } = constants.LIMITS;

// What a missing field stands for, core/models.hpp's Line defaults.
export const LINE_DEFAULTS = Object.freeze({
  color: '#FFFF00', thickness: 2, pointSize: 4, style: 'solid',
  locked: false, fillColor: 'transparent', pointColor: '',
});
const DEFAULTS = LINE_DEFAULTS;
const TEXT_FIELDS = ['color', 'style', 'fillColor', 'pointColor'];

export const encodeLines = (lines) => {
  const rows = (Array.isArray(lines) ? lines : []).map((line) => ({
    line: line || {},
    points: (line && line.points) || [],
    text: TEXT_FIELDS.map((f) => utf8.encode(String((line && line[f]) ?? DEFAULTS[f]))),
  }));
  const numsLen = rows.reduce((n, r) => n + 8 + 2 * r.points.length, 1);
  const textLen = rows.reduce((n, r) => n + r.text.reduce((m, b) => m + b.length, 0), 0);
  const nums = new Float64Array(numsLen);
  const text = new Uint8Array(textLen);
  let i = 0, t = 0;
  nums[i++] = rows.length;
  for (const r of rows) {
    nums[i++] = r.points.length;
    nums[i++] = r.line.thickness ?? DEFAULTS.thickness;
    nums[i++] = r.line.pointSize ?? DEFAULTS.pointSize;
    nums[i++] = (r.line.locked ?? DEFAULTS.locked) ? 1 : 0;
    for (const bytes of r.text) nums[i++] = bytes.length;
    for (const p of r.points) { nums[i++] = p.x; nums[i++] = p.y; }
    for (const bytes of r.text) { text.set(bytes, t); t += bytes.length; }
  }
  return { nums, text };
};

// Lengths are honoured, never trusted: a truncated snapshot stops at the last complete line.
// Counts are range-checked before they are used (NaN and 1e300 never become a loop bound), and
// the caps cut as sanitizeLines does: the line spending the last point is cut, the rest dropped.
export const decodeLines = (nums, text) => {
  const out = [];
  if (!nums || nums.length < 1 || !(nums[0] > 0)) return out;
  const textLen = text ? text.length : 0;
  const lineCount = nums[0] < MAX_LINES ? Math.trunc(nums[0]) : MAX_LINES;
  let i = 1, t = 0, budget = MAX_POINTS;
  for (let li = 0; li < lineCount && budget > 0; li++) {
    if (i + 8 > nums.length) break;
    const declared = nums[i];
    const line = {
      points: [],
      thickness: nums[i + 1],
      pointSize: nums[i + 2],
      locked: nums[i + 3] !== 0,
    };
    let ok = true;
    const len = [0, 0, 0, 0].map((_, f) => {
      const l = nums[i + 4 + f];
      ok = ok && l >= 0 && l <= textLen - t;
      return ok ? Math.trunc(l) : 0;
    });
    i += 8;
    if (!(declared >= 0) || i + 2 * declared > nums.length) break;
    const ptCount = Math.trunc(declared);
    const kept = Math.min(ptCount, MAX_LINE_POINTS, budget);
    for (let p = 0; p < kept; p++) line.points.push({ x: nums[i + 2 * p], y: nums[i + 2 * p + 1] });
    i += 2 * ptCount;
    for (let f = 0; f < TEXT_FIELDS.length && ok; f++) {
      if (!text || t + len[f] > textLen) { ok = false; break; }
      line[TEXT_FIELDS[f]] = utf8Decode.decode(text.subarray(t, t + len[f]));
      t += len[f];
    }
    if (!ok) break;
    budget -= kept;
    out.push(line);
  }
  return out;
};
