import { test } from 'node:test';
import assert from 'node:assert';
import { distToSegment, distToSegmentSq } from '../../../js/utils.js';

test('zero-length segment returns distance to the point', () => {
    const a = { x: 5, y: 5 };
    const b = { x: 5, y: 5 };
    assert.strictEqual(distToSegment(8, 9, a, b), Math.hypot(3, 4)); // 5
});

test('point exactly on segment is ~0', () => {
    const a = { x: 0, y: 0 };
    const b = { x: 10, y: 0 };
    assert.ok(distToSegment(5, 0, a, b) < 1e-9);
});

test('perpendicular distance to mid-segment', () => {
    const a = { x: 0, y: 0 };
    const b = { x: 10, y: 0 };
    assert.strictEqual(distToSegment(5, 4, a, b), 4);
});

test('projection beyond endpoint a (t clamped to 0)', () => {
    const a = { x: 0, y: 0 };
    const b = { x: 10, y: 0 };
    assert.strictEqual(distToSegment(-3, 0, a, b), 3);
});

test('projection beyond endpoint b (t clamped to 1)', () => {
    const a = { x: 0, y: 0 };
    const b = { x: 10, y: 0 };
    assert.strictEqual(distToSegment(14, 0, a, b), 4);
});

// Twin: core/tests/geometry/pointMath.test.cpp.
test('distToSegmentSq is the squared distance, exact on integer cases', () => {
    const a = { x: 0, y: 0 };
    const b = { x: 10, y: 0 };
    assert.strictEqual(distToSegmentSq(5, 4, a, b), 16);
    assert.strictEqual(distToSegmentSq(-3, 4, a, b), 25);
    assert.strictEqual(distToSegmentSq(13, 4, a, b), 25);
    assert.strictEqual(distToSegmentSq(8, 9, { x: 5, y: 5 }, { x: 5, y: 5 }), 25);
    const d = distToSegment(2, 7, { x: 1, y: 1 }, { x: 9, y: 4 });
    assert.ok(Math.abs(distToSegmentSq(2, 7, { x: 1, y: 1 }, { x: 9, y: 4 }) - d * d) < 1e-9);
});
