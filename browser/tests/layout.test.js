import { test } from 'node:test';
import assert from 'node:assert';
import { buildLayoutPayload, validateLayout, resolveInsertIdx, fillState, defaultBlankSizePx } from '../js/core/layout.js';

// ── buildLayoutPayload ──────────────────────────────────────────
test('buildLayoutPayload passes the lines array through by reference', () => {
    const lines = [{ points: [{ x: 1, y: 2 }] }];
    const out = buildLayoutPayload({ imageWidth: 10, imageHeight: 20, lines });
    assert.strictEqual(out.lines, lines); // same reference, no copy
});

test('buildLayoutPayload preserves the dimension numbers', () => {
    const out = buildLayoutPayload({ imageWidth: 800, imageHeight: 600, lines: [] });
    assert.strictEqual(out.imageWidth, 800);
    assert.strictEqual(out.imageHeight, 600);
});

test('buildLayoutPayload serializes byte-identically to the old inline literal', () => {
    const payload = buildLayoutPayload({ imageWidth: 800, imageHeight: 600, lines: [{ points: [{ x: 1, y: 2 }] }] });
    const expected =
`{
  "imageWidth": 800,
  "imageHeight": 600,
  "lines": [
    {
      "points": [
        {
          "x": 1,
          "y": 2
        }
      ]
    }
  ]
}`;
    assert.strictEqual(JSON.stringify(payload, null, 2), expected);
});

// ── validateLayout ──────────────────────────────────────────────
test('validateLayout rejects when no image is loaded', () => {
    const r = validateLayout({ lines: [] }, { hasImage: false, imgW: 10, imgH: 10, hasExistingLines: false });
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.reason, 'no-image');
});

test('validateLayout: image, no existing lines, matching dims → clean apply', () => {
    const lines = [{ points: [] }];
    const r = validateLayout({ imageWidth: 100, imageHeight: 50, lines }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.needsReplaceConfirm, false);
    assert.strictEqual(r.needsDimMismatchConfirm, false);
    assert.strictEqual(r.lines, lines);
});

test('validateLayout flags replace-confirm when lines already exist', () => {
    const r = validateLayout({ imageWidth: 100, imageHeight: 50, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: true });
    assert.strictEqual(r.needsReplaceConfirm, true);
});

test('validateLayout flags dim-mismatch when width differs', () => {
    const r = validateLayout({ imageWidth: 99, imageHeight: 50, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.strictEqual(r.needsDimMismatchConfirm, true);
});

test('validateLayout flags dim-mismatch when height differs', () => {
    const r = validateLayout({ imageWidth: 100, imageHeight: 49, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.strictEqual(r.needsDimMismatchConfirm, true);
});

test('validateLayout defaults lines to [] when the field is missing', () => {
    const r = validateLayout({ imageWidth: 100, imageHeight: 50 }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.deepStrictEqual(r.lines, []);
});

test('validateLayout can flag both replace and dim-mismatch at once', () => {
    const r = validateLayout({ imageWidth: 1, imageHeight: 2, lines: [] }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: true });
    assert.strictEqual(r.needsReplaceConfirm, true);
    assert.strictEqual(r.needsDimMismatchConfirm, true);
});

// ── resolveInsertIdx ────────────────────────────────────────────
const line4 = { points: [{}, {}, {}, {}] }; // length 4

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

test('fillState: no default supplied falls back to #3399ff', () => {
    assert.deepStrictEqual(fillState({}, undefined), { enabled: false, value: '#3399ff' });
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
