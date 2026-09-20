// js/core/layout.js helpers: resolveInsertIdx, fillState, defaultBlankSizePx and sanitizeLines'
// pointSize spelling. Split from layout.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { resolveInsertIdx, fillState, defaultBlankSizePx, sanitizeLines } from '../js/core/layout.js';

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
    const canon = JSON.parse(readFileSync(new URL('../js/config/constants.json', import.meta.url), 'utf8'));
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
