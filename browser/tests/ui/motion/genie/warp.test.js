// The genie warp (js/ui/motion/genie/warp.js), the port of desktop support/genie/genieWarp.hpp: the
// flow side, the window whole at rest, the near edge leading into a funnel, the bands joined, and the
// window on the icon at the end. The extension port's cases.
import test from 'node:test';
import assert from 'node:assert';

import { genieFlowSide, genieWarp, genieTravel, genieSink, GENIE_MAX_ICON_PX } from '../../../../js/ui/motion/genie/warp.js';

const WIN = { left: 100, top: 200, width: 400, height: 300 };   // x 100–500, y 200–500
const icon = (left, top) => ({ left, top, width: 24, height: 24 });
const near = (a, b) => Math.abs(a - b) < 1e-6;
const union = (rects) => rects.reduce((u, r) => ({
  left: Math.min(u.left, r.left), top: Math.min(u.top, r.top),
  right: Math.max(u.right, r.left + r.width), bottom: Math.max(u.bottom, r.top + r.height),
}), { left: Infinity, top: Infinity, right: -Infinity, bottom: -Infinity });
const ICONS = { down: icon(280, 700), up: icon(280, 40), right: icon(800, 300), left: icon(-100, 300) };

test('genieFlowSide: past an edge the widest gap wins; over the window, the way it lies', () => {
  for (const [side, at] of Object.entries(ICONS)) assert.strictEqual(genieFlowSide(WIN, at), side);
  assert.strictEqual(genieFlowSide(WIN, icon(10, 520)), 'left');
  assert.strictEqual(genieFlowSide(WIN, icon(280, 220)), 'up');
  assert.strictEqual(genieFlowSide(WIN, icon(130, 330)), 'left');
});

test('the near edge leads and the far edge trails; both are home at the ends', () => {
  assert.strictEqual(genieTravel(0, 0), 0);
  assert.strictEqual(genieTravel(1, 1), 1);
  assert.strictEqual(genieTravel(0, 1), 1);
  assert.ok(genieTravel(1, 0.3) > genieTravel(0.5, 0.3) && genieTravel(0.5, 0.3) > genieTravel(0, 0.3));
});

for (const [side, at] of Object.entries(ICONS)) {
  test(`flowing ${side}: whole at rest, pinched toward the icon, joined, and on the icon at the end`, () => {
    const rest = genieWarp(WIN, at, side, 0);
    const u = union(rest.map((b) => b.target));
    assert.ok(near(u.left, 100) && near(u.right, 500) && near(u.top, 200) && near(u.bottom, 500), 'covers the window');
    assert.ok(near(rest.reduce((s, b) => s + b.source.w * b.source.h, 0), 1), 'reads the picture once');

    const vertical = side === 'up' || side === 'down';
    const across = (b) => (vertical ? b.target.width : b.target.height);
    const mid = genieWarp(WIN, at, side, 0.35);
    const first = mid[0], last = mid[mid.length - 1];
    assert.ok(across(last) < across(first) * 0.5, 'the near end is pinched');

    const late = genieWarp(WIN, at, side, 0.7);
    for (let k = 0; k + 1 < late.length; k++) {
      const a = late[k].target, b = late[k + 1].target;
      const gap = vertical ? Math.min(Math.abs(a.top - (b.top + b.height)), Math.abs(a.top + a.height - b.top))
                           : Math.min(Math.abs(a.left - (b.left + b.width)), Math.abs(a.left + a.width - b.left));
      assert.ok(gap < 1e-6, 'no band parts from its neighbour');
    }
    const end = union(genieWarp(WIN, at, side, 1).map((b) => b.target));
    assert.ok(near(end.left, at.left) && near(end.top, at.top) && near(end.right, at.left + 24)
      && near(end.bottom, at.top + 24), 'lies on the icon');
  });
}

test('genieSink: an icon is kept; a region (the canvas a window pours into) becomes a point at its centre', () => {
  const ic = icon(10, 10);
  assert.strictEqual(genieSink(ic), ic);
  const canvas = { left: 0, top: 100, width: 800, height: 500 };
  assert.ok(canvas.width > GENIE_MAX_ICON_PX);
  assert.deepStrictEqual(genieSink(canvas), { left: 388, top: 338, width: 24, height: 24 });
});

// The silhouette the theme swap's Sliding mask plays: a fixed vertex count, the window at rest, the icon at
// the end (desktop genieWarp.hpp outline pins these same points).
test('genieOutline: a fixed count of points, the window at rest and the icon at the end', async () => {
  const { genieOutline } = await import('../../../../js/ui/motion/genie/warp.js');
  const win = { left: 100, top: 200, width: 400, height: 300 }, icon = { left: 280, top: 40, width: 24, height: 24 };
  const mid = genieOutline(win, icon, 'up', 0.5, 24);
  assert.equal(mid.length, 50);
  assert.ok(Math.abs(mid[12].x - 190) < 1e-3 && Math.abs(mid[12].y - 201) < 1e-3);
  const rest = genieOutline(win, icon, 'up', 0, 24);
  assert.deepEqual([rest[0].y, rest[24].y, rest[24].x, rest[25].x], [500, 200, 100, 500]);
  const end = genieOutline(win, icon, 'up', 1, 24);
  assert.ok(end.every((q) => q.x >= 280 - 1e-6 && q.x <= 304 + 1e-6 && q.y >= 40 - 1e-6 && q.y <= 64 + 1e-6));
});

