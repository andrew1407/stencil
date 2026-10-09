// A hidden committed line offers no mark: every finder over the lines as drawn answers what it
// answered over the old blank stand-in (each hidden line swapped for a mark-less one, indices kept).
// Mirrors core/tests/geometry/hitTestHidden.test.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';

import {
  findLineAt, findNearestPoint, findNearestPointWithIdx, findNearestSegmentWithIdx,
} from '../../js/core/draw/hitTest.js';
import { holdDrawTarget } from '../../js/core/draw/holdDraw.js';

const pts = (...xy) => xy.map(([x, y]) => ({ x, y }));
const blanked = (lines) => lines.map((l) => (l.hidden ? { points: [] } : l));

test('a hidden line is hit exactly as its old blank stand-in was, on every side of the stack', () => {
  const strokes = [pts([0, 0], [100, 0]), pts([0, 3], [100, 3]), pts([0, 60], [100, 60]), pts([50, -40], [50, 40])];
  const stacks = [];
  for (let mask = 0; mask < 16; mask++)
    stacks.push(strokes.map((points, i) => ({ points, hidden: Boolean(mask & (1 << i)) })));
  stacks.push([], [{ points: [], hidden: true }, { points: strokes[0] }]);
  const current = [null, { points: pts([50, 1]) }];
  const probes = [[0, 0], [50, 1], [50, 2], [100, 3], [50, 60], [50, -30], [200, 200], [-5, 0]];
  let compared = 0;
  for (const lines of stacks) {
    const old = blanked(lines);
    for (const [x, y] of probes)
      for (const r of [0, 2.5, 8, 12]) {
        assert.equal(findLineAt(lines, x, y, r), findLineAt(old, x, y, r));
        for (const cur of current) {
          assert.deepEqual(findNearestPoint(lines, cur, x, y, r), findNearestPoint(old, cur, x, y, r));
          assert.deepEqual(findNearestPointWithIdx(lines, cur, x, y, r), findNearestPointWithIdx(old, cur, x, y, r));
        }
        assert.deepEqual(findNearestSegmentWithIdx(lines, x, y, r), findNearestSegmentWithIdx(old, x, y, r));
        const opts = { pointThreshold: r, segThreshold: r };
        assert.deepEqual(holdDrawTarget(lines, x, y, opts), holdDrawTarget(old, x, y, opts));
        compared++;
      }
  }
  assert.equal(compared, 18 * 8 * 4);
});

test('a hidden line contributes nothing and the indices stay the lines\' own', () => {
  const lines = [{ points: pts([0, 0], [100, 0]) }, { points: pts([0, 0], [100, 0]), hidden: true }];
  assert.equal(findLineAt(lines, 50, 1, 8), 0);
  assert.equal(findNearestPointWithIdx(lines, null, 0, 0, 12).lineIdx, 0);
  assert.equal(findNearestSegmentWithIdx(lines, 50, 1, 12).lineIdx, 0);
  assert.equal(holdDrawTarget(lines, 50, 1).lineIdx, 0);
  assert.equal(findLineAt([lines[1]], 50, 1, 8), -1);
  assert.equal(holdDrawTarget([lines[1]], 0, 0).kind, 'new');
});
