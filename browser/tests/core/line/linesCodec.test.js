// decodeLines is the twin of core/abi/linesCodec.hpp: lengths honoured, never trusted, and the
// layout caps of sanitizeLines (50k lines, 100k points a line, 1M points in all) applied the same.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { encodeLines, decodeLines } from '../../../js/core/line/linesCodec.js';

const pts = (n) => Array.from({ length: n }, (_, i) => ({ x: i, y: -i }));
const line = (n, color = '#ff0000') => ({ points: pts(n), color, style: 'dashed', thickness: 3, pointSize: 5 });

test('a snapshot round-trips field for field', () => {
  const { nums, text } = encodeLines([line(3), { points: [], color: 'é' }]);
  const out = decodeLines(nums, text);
  assert.equal(out.length, 2);
  assert.deepEqual(out[0].points, pts(3));
  assert.equal(out[0].style, 'dashed');
  assert.equal(out[1].color, 'é');
  assert.equal(out[1].fillColor, 'transparent');
});

test("a line's name and hidden flag round-trip; a missing one decodes as unnamed and shown", () => {
  const { nums, text } = encodeLines([{ ...line(2), name: 'Roof ridge', hidden: true }, line(1)]);
  const out = decodeLines(nums, text);
  assert.equal(out[0].name, 'Roof ridge');
  assert.equal(out[0].hidden, true);
  assert.deepEqual(out[0].points, pts(2));
  assert.equal(out[1].name, '');
  assert.equal(out[1].hidden, false);
  assert.equal(out[1].color, '#ff0000');
});

test('NaN, negative and huge counts never drive the decode', () => {
  const { nums, text } = encodeLines([line(2), line(1)]);
  for (const bad of [NaN, -1, 0]) {
    const n = nums.slice(); n[0] = bad;
    assert.deepEqual(decodeLines(n, text), [], `line count ${bad}`);
  }
  const huge = nums.slice(); huge[0] = 1e300;
  assert.equal(decodeLines(huge, text).length, 2, 'a huge line count is capped, the buffer ends it');
  const nanPts = nums.slice(); nanPts[1] = NaN;
  assert.deepEqual(decodeLines(nanPts, text), [], 'a NaN point count stops at the last whole line');
  const nanLen = nums.slice(); nanLen[6] = NaN;
  assert.deepEqual(decodeLines(nanLen, text), [], 'a NaN text length is malformed');
  assert.deepEqual(decodeLines(nums, undefined), [], 'no text buffer, no line');
});

test('a fractional count is truncated as the core casts it', () => {
  const { nums, text } = encodeLines([line(2)]);
  const n = nums.slice(); n[0] = 1.9;
  assert.equal(decodeLines(n, text).length, 1);
});

test('the per-line and total point caps cut exactly as sanitizeLines does', () => {
  const big = pts(100_005);
  const lines = [{ points: pts(50) }, ...Array.from({ length: 11 }, () => ({ points: big, color: '#0000ff' }))];
  const { nums, text } = encodeLines(lines);
  const out = decodeLines(nums, text);
  assert.equal(out.length, 11, 'the line after the budget is spent is dropped');
  assert.equal(out[1].points.length, 100_000, 'the per-line cap');
  assert.equal(out[1].color, '#0000ff', 'the skipped points do not shift the text');
  assert.equal(out[10].points.length, 100_000 - 50, 'the crossing line keeps what the budget allows');
  assert.equal(out.reduce((n, l) => n + l.points.length, 0), 1_000_000);
});
