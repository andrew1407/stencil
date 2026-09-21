// validateLayout (js/core/layout.js): the apply/confirm flags and the sanitization every
// untrusted ingress (#stencil=, paste, co-edit) passes through. Split from layout.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { validateLayout } from '../../js/core/layout.js';

// ── validateLayout ──────────────────────────────────────────────
test('validateLayout rejects when no image is loaded', () => {
    const r = validateLayout({ lines: [] }, { hasImage: false, imgW: 10, imgH: 10, hasExistingLines: false });
    assert.strictEqual(r.ok, false);
    assert.strictEqual(r.reason, 'no-image');
});

test('validateLayout: image, no existing lines, matching dims → clean apply', () => {
    const lines = [{ points: [{ x: 1, y: 2 }], color: '#f00' }];
    const r = validateLayout({ imageWidth: 100, imageHeight: 50, lines }, { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false });
    assert.strictEqual(r.ok, true);
    assert.strictEqual(r.needsReplaceConfirm, false);
    assert.strictEqual(r.needsDimMismatchConfirm, false);
    // lines are sanitized (rebuilt on a fresh array), not passed through by reference.
    assert.notStrictEqual(r.lines, lines);
    assert.deepStrictEqual(r.lines, [{ points: [{ x: 1, y: 2 }], color: '#f00', locked: false }]);
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

// ── validateLayout sanitization (untrusted #stencil= / paste / co-edit ingress) ──
const ctx = { hasImage: true, imgW: 100, imgH: 50, hasExistingLines: false };

test('validateLayout: a __proto__-carrying line never pollutes Object.prototype', () => {
    const evil = JSON.parse('{"imageWidth":100,"imageHeight":50,"lines":[{"points":[],"__proto__":{"polluted":1},"constructor":{"x":1}}]}');
    const r = validateLayout(evil, ctx);
    assert.strictEqual(({}).polluted, undefined);   // no global pollution
    assert.strictEqual(({}).x, undefined);
    // the rebuilt line carries only whitelisted fields (no __proto__/constructor own key)
    assert.deepStrictEqual(Object.keys(r.lines[0]).sort(), ['locked', 'points']);
});

test('validateLayout: non-array lines → [] and non-object data is tolerated', () => {
    assert.deepStrictEqual(validateLayout({ imageWidth: 100, imageHeight: 50, lines: 'nope' }, ctx).lines, []);
    assert.deepStrictEqual(validateLayout({ imageWidth: 100, imageHeight: 50, lines: { 0: 'x' } }, ctx).lines, []);
    assert.deepStrictEqual(validateLayout(null, ctx).lines, []);
});

test('validateLayout: drops non-finite/non-numeric point coords', () => {
    const data = { imageWidth: 100, imageHeight: 50, lines: [{ points: [
        { x: 1, y: 2 }, { x: 'a', y: 3 }, { x: Infinity, y: 0 }, { x: NaN, y: NaN }, { x: 5, y: 6 },
    ] }] };
    assert.deepStrictEqual(validateLayout(data, ctx).lines[0].points, [{ x: 1, y: 2 }, { x: 5, y: 6 }]);
});

test('validateLayout: caps a hostile explosion of lines and points', () => {
    const manyLines = { imageWidth: 100, imageHeight: 50, lines: Array.from({ length: 50010 }, () => ({ points: [] })) };
    assert.strictEqual(validateLayout(manyLines, ctx).lines.length, 50000);

    const manyPoints = { imageWidth: 100, imageHeight: 50, lines: [{ points: Array.from({ length: 100010 }, (_, i) => ({ x: i, y: i })) }] };
    assert.strictEqual(validateLayout(manyPoints, ctx).lines[0].points.length, 100000);
});

