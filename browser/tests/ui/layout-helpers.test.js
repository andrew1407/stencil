// js/core/layout.js helpers: resolveInsertIdx, fillState, defaultBlankSizePx, sanitizeLines'
// pointSize spelling and the caps on a joined layout. Split from layout.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { resolveInsertIdx, fillState, defaultBlankSizePx, sanitizeLines, capLayoutPoints } from '../../js/core/layout.js';

// ── resolveInsertIdx ────────────────────────────────────────────
const line4 = { points: [{}, {}, {}, {}] };

test('resolveInsertIdx returns focusedPtIdx+1 when the focused point is on the selected line', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 2, selectedLineIdx: 2, focusedPtIdx: 2 }), 3);
});

test('resolveInsertIdx appends when coordLineIdx differs from selectedLineIdx', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 1, selectedLineIdx: 2, focusedPtIdx: 2 }), 4);
});

test('resolveInsertIdx appends when no point is focused (-1)', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 2, selectedLineIdx: 2, focusedPtIdx: -1 }), 4);
});

test('resolveInsertIdx handles focusedPtIdx===0 boundary', () => {
    assert.strictEqual(resolveInsertIdx(line4, { coordLineIdx: 2, selectedLineIdx: 2, focusedPtIdx: 0 }), 1);
});

test('resolveInsertIdx falls back to length 0 for an empty line', () => {
    assert.strictEqual(resolveInsertIdx({ points: [] }, { coordLineIdx: 0, selectedLineIdx: 0, focusedPtIdx: -1 }), 0);
});

// ── fillState ───────────────────────────────────────────────────
test('fillState: undefined fillColor → disabled, value is the default', () => {
    assert.deepStrictEqual(fillState({}, '#112233'), { enabled: false, value: '#112233' });
});

test('fillState: transparent fillColor → disabled, value is the default', () => {
    assert.deepStrictEqual(fillState({ fillColor: 'transparent' }, '#112233'), { enabled: false, value: '#112233' });
});

test('fillState: a real fill color → enabled, value is that color', () => {
    assert.deepStrictEqual(fillState({ fillColor: '#abcdef' }, '#112233'), { enabled: true, value: '#abcdef' });
});

// White, not blue: a fill is paint you put ON the picture, so neutral is the least surprising start. Held
// against the shared constants, so this fallback and the app-wide default cannot drift apart.
test('fillState: no default supplied falls back to the shared default (white)', () => {
    assert.deepStrictEqual(fillState({}, undefined), { enabled: false, value: '#ffffff' });
    const canon = JSON.parse(readFileSync(new URL('../../js/config/constants.json', import.meta.url), 'utf8'));
    assert.equal(canon.DEFAULT_VISUALS.defaultFillColor, '#ffffff');
});

// ── defaultBlankSizePx ──────────────────────────────────────────
test('defaultBlankSizePx renders A4/A3 pages at 96 dpi', () => {
    assert.deepStrictEqual(defaultBlankSizePx({ width: 21, height: 29.7 }), { width: 794, height: 1123 });
    assert.deepStrictEqual(defaultBlankSizePx({ width: 29.7, height: 42 }), { width: 1123, height: 1587 });
});

test('defaultBlankSizePx honors a custom dpi', () => {
    assert.deepStrictEqual(defaultBlankSizePx({ width: 2.54, height: 5.08 }, 100), { width: 100, height: 200 });
});

test('defaultBlankSizePx never collapses below 1px', () => {
    assert.deepStrictEqual(defaultBlankSizePx({ width: 0, height: 0.001 }), { width: 1, height: 1 });
});

// `markerSize` is not an accepted alias for `pointSize`: readers know only `pointSize`, and writers only
// ever emit `pointSize`.
test('sanitizeLines ignores the old markerSize spelling', () => {
  const [line] = sanitizeLines([{ points: [{ x: 1, y: 2 }], markerSize: 9 }]);
  assert.ok(!('pointSize' in line), 'markerSize is not read as pointSize');
  assert.ok(!('markerSize' in line), 'and it is not carried through either');
});

test('sanitizeLines reads pointSize', () => {
  const [line] = sanitizeLines([{ points: [{ x: 1, y: 2 }], pointSize: 9 }]);
  assert.strictEqual(line.pointSize, 9);
});

// CO-5: 50k lines × 100k points would fit per-line caps yet hold 5e9 points; the total is capped too.
test('sanitizeLines caps the TOTAL points: the line that spends the budget is cut, later lines dropped', () => {
  const full = Array.from({ length: 100_000 }, (_, i) => ({ x: i, y: 1 }));
  const raw = [{ points: full.slice(0, 50) }, ...Array.from({ length: 11 }, () => ({ points: full }))];
  const out = sanitizeLines(raw);
  const total = out.reduce((n, l) => n + l.points.length, 0);
  assert.strictEqual(total, 1_000_000);
  assert.strictEqual(out.length, 11, 'the 12th line comes after the budget is spent');
  assert.strictEqual(out[10].points.length, 100_000 - 50, 'the crossing line keeps what the budget allows');
  assert.strictEqual(out[1].points.length, 100_000, 'the per-line cap still holds before that');
});

// Two capped halves (a combine, a co-edit union) can pass the total; the join is cut as one layout.
test('capLayoutPoints cuts a joined layout where the total runs out, keeping the lines before it', () => {
  const full = Array.from({ length: 100_000 }, (_, i) => ({ x: i, y: 1 }));
  const half = Array.from({ length: 6 }, () => ({ points: full, color: '#f00' }));
  const joined = [...half, ...half];
  const out = capLayoutPoints(joined);
  assert.strictEqual(out.reduce((n, l) => n + l.points.length, 0), 1_000_000);
  assert.strictEqual(out.length, 10);
  assert.strictEqual(out[0], joined[0], 'a line under the budget is the same object');
  assert.deepStrictEqual(out[9], { points: full, color: '#f00' }, 'the line that spends it exactly stays whole');
  assert.strictEqual(capLayoutPoints([{ points: full }, { points: [] }]).length, 2);
});

test('capLayoutPoints cuts the crossing line and returns the same array under the caps', () => {
  const pts = (n) => Array.from({ length: n }, (_, i) => ({ x: i, y: 0 }));
  const joined = [{ points: pts(999_990) }, { points: pts(25), color: '#0f0' }, { points: pts(3) }];
  const out = capLayoutPoints(joined);
  assert.deepStrictEqual(out.map((l) => l.points.length), [999_990, 10]);
  assert.strictEqual(out[1].color, '#0f0');
  assert.strictEqual(joined[1].points.length, 25, 'the input line is not cut in place');
  const small = [{ points: pts(2) }];
  assert.strictEqual(capLayoutPoints(small), small);
  assert.strictEqual(capLayoutPoints(Array.from({ length: 50_001 }, () => ({ points: [] }))).length, 50_000);
});
