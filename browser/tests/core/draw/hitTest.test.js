// js/core/draw/hitTest.js: the bbox reject and the squared-distance compare change no answer.
// Twin cases: core/tests/geometry/hitTest.test.cpp, which pins core findNearestPoint/Segment alike.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  findLineAt, findNearestPoint, findNearestPointWithIdx, findNearestSegmentWithIdx,
} from '../../../js/core/draw/hitTest.js';
import { holdDrawTarget } from '../../../js/core/draw/holdDraw.js';
import { distToSegment } from '../../../js/utils.js';

const scatter = () => Array.from({ length: 40 }, (_, i) => ({
  points: Array.from({ length: 5 }, (_, k) => ({
    x: (i * 37 + k * 11) % 300, y: (i * 53 + k * 7) % 300,
  })),
}));

test('findNearestPointWithIdx / findNearestSegmentWithIdx agree with a brute scan', () => {
  const lines = scatter();
  let pointHits = 0, segmentHits = 0;
  for (let y = 0; y < 300; y += 7) {
    for (let x = 0; x < 300; x += 7) {
      let point = null, seg = null, best = 12;
      for (let li = lines.length - 1; li >= 0; li--) {
        const pts = lines[li].points;
        for (let pi = 0; pi < pts.length && !point; pi++) {
          if (Math.hypot(pts[pi].x - x, pts[pi].y - y) < 12)
            point = { lineIdx: li, ptIdx: pi, point: pts[pi] };
        }
        for (let j = 0; j + 1 < pts.length; j++) {
          const d = distToSegment(x, y, pts[j], pts[j + 1]);
          if (d < best) { best = d; seg = { lineIdx: li, ptIdx1: j, ptIdx2: j + 1 }; }
        }
      }
      assert.deepEqual(findNearestPointWithIdx(lines, null, x, y, 12), point);
      assert.deepEqual(findNearestSegmentWithIdx(lines, x, y, 12), seg);
      pointHits += point ? 1 : 0;
      segmentHits += seg ? 1 : 0;
    }
  }
  assert.ok(pointHits > 20 && segmentHits > 100, 'the scatter exercises hits as well as rejects');
});

test('the point scans are strict at the threshold and find nothing for a non-positive one', () => {
  const one = [{ points: [{ x: 0, y: 0 }] }];
  for (const find of [
    (t, x, y) => findNearestPoint(one, null, x, y, t),
    (t, x, y) => findNearestPointWithIdx(one, null, x, y, t),
  ]) {
    assert.equal(find(5, 3, 4), null);  // distance exactly 5
    assert.notEqual(find(5.001, 3, 4), null);
    assert.equal(find(0, 0, 0), null);
    assert.equal(find(-5, 0, 0), null);
  }
});

test('the in-progress line is scanned first by index, last by point', () => {
  const lines = [{ points: [{ x: 10, y: 10 }] }];
  const current = { points: [{ x: 11, y: 11 }] };
  assert.equal(findNearestPointWithIdx(lines, current, 10, 10, 12).lineIdx, -1);
  assert.equal(findNearestPoint(lines, current, 10, 10, 12), lines[0].points[0]);
});

// Twin: the segment-scan boundary case in core/tests/geometry/hitTest.test.cpp.
test('the segment scans hold their boundary at the threshold, nothing for a non-positive one', () => {
  const seg = [{ points: [{ x: 0, y: 0 }, { x: 10, y: 0 }] }];
  assert.equal(findNearestSegmentWithIdx(seg, 5, 5, 5), null);  // distance exactly 5
  assert.notEqual(findNearestSegmentWithIdx(seg, 5, 5, 5.001), null);
  assert.equal(findNearestSegmentWithIdx(seg, 5, 0, 0), null);
  assert.equal(findNearestSegmentWithIdx(seg, 5, 0, -5), null);
  assert.equal(findLineAt(seg, 5, 5, 5), 0);                     // inclusive at the threshold
  assert.equal(findLineAt(seg, 13, 4, 1), 0);                    // point radius 5, distance 5
  assert.equal(findLineAt(seg, 5, 0, -3), -1);                   // no radius below zero
  assert.equal(holdDrawTarget(seg, 5, 5, { pointThreshold: 1, segThreshold: 5 }).kind, 'new');
  assert.equal(holdDrawTarget(seg, 5, 5, { pointThreshold: 1, segThreshold: 5.001 }).kind, 'segment');
});
