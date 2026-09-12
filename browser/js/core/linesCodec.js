// Twin of core/abi/linesCodec.hpp, byte layout and all: a snapshot crosses the core ABI as
//   nums: [lineCount, then per line: pointCount, thickness, pointSize, locked,
//          byte lengths of color/style/fillColor/pointColor, then x0,y0,x1,y1,…]
//   text: those four strings per line, concatenated UTF-8, in that field order.
// A line missing a field encodes as the core's default for it (core/models.hpp).

const utf8 = new TextEncoder();
const utf8Decode = new TextDecoder();

const DEFAULTS = {
  color: '#FFFF00', thickness: 2, pointSize: 4, style: 'solid',
  locked: false, fillColor: 'transparent', pointColor: '',
};
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
export const decodeLines = (nums, text) => {
  const out = [];
  if (!nums || nums.length < 1) return out;
  const lineCount = nums[0];
  let i = 1, t = 0;
  for (let li = 0; li < lineCount; li++) {
    if (i + 8 > nums.length) break;
    const ptCount = nums[i];
    const line = {
      points: [],
      thickness: nums[i + 1],
      pointSize: nums[i + 2],
      locked: nums[i + 3] !== 0,
    };
    const len = [nums[i + 4], nums[i + 5], nums[i + 6], nums[i + 7]];
    i += 8;
    if (ptCount < 0 || i + 2 * ptCount > nums.length) break;
    for (let p = 0; p < ptCount; p++) line.points.push({ x: nums[i + 2 * p], y: nums[i + 2 * p + 1] });
    i += 2 * ptCount;
    let ok = true;
    for (let f = 0; f < TEXT_FIELDS.length; f++) {
      if (len[f] < 0 || t + len[f] > text.length) { ok = false; break; }
      line[TEXT_FIELDS[f]] = utf8Decode.decode(text.subarray(t, t + len[f]));
      t += len[f];
    }
    if (!ok) break;
    out.push(line);
  }
  return out;
};
