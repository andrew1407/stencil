// The aqua table's pure rules (js/ui/aqua/rules.js): the wallpaper's ramp and swooshes on the
// cell grid, its runs covering every cell once, and the word in the skin's own gel colours.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AQUA, wallGrid, rampAt, swooshDepth, wallColorAt, wallRuns, wordLines, SKIN_NAME, PROJECT_NAME } from '../../../js/ui/aqua/rules.js';
import { WEBCORE } from '../../../js/ui/webcore/rules.js';

test('the grid is the picture in cells, the ramp runs top to bottom', () => {
  const grid = wallGrid();
  assert.deepEqual(grid, { cell: 2, cols: 512, rows: 384 });
  assert.equal(rampAt(0, grid), AQUA.image.ramp[0]);
  assert.equal(rampAt(grid.rows - 1, grid), AQUA.image.ramp.at(-1));
  assert.equal(SKIN_NAME, 'aqua');
  assert.equal(PROJECT_NAME, 'somefruitcompany');
});

test('a swoosh is a parabola: its crest mid-span, nothing outside its span or below its depth', () => {
  const s = AQUA.image.swooshes[0];
  const mid = s.x0 + s.span / 2;
  assert.equal(swooshDepth(s, mid, s.base - s.amp), 0, 'the crest sits amp above base');
  assert.equal(swooshDepth(s, mid, s.base - s.amp - 1), -1);
  assert.equal(swooshDepth(s, mid, s.base - s.amp + s.width), -1);
  assert.equal(swooshDepth(s, s.x0 - 1, s.base), -1);
});

test('a swoosh core takes its colour, its rim a checker over the ramp', () => {
  const grid = wallGrid();
  const s = AQUA.image.swooshes[0];
  const cx = s.x0 + s.span / 2 + 1;
  const top = s.base - Math.floor((s.amp * 4 * (cx - s.x0) * (s.span - (cx - s.x0))) / (s.span * s.span));
  assert.equal(wallColorAt(cx, top + s.edge + 1, grid), s.color);
  const rim = [wallColorAt(cx, top, grid), wallColorAt(cx, top + 1, grid)];
  assert.ok(rim.includes(s.edgeColor) && rim.some((c) => c !== s.edgeColor), 'the rim dithers');
});

test('the runs cover every cell exactly once', () => {
  const grid = wallGrid();
  const runs = wallRuns(grid);
  assert.equal(runs.reduce((n, r) => n + r.w * r.h, 0), grid.cols * grid.rows);
  for (const r of runs) assert.equal(r.color, wallColorAt(r.x, r.y, grid));
});

test('the word is the webcore glyphs in seven gel colours, locked and filled', () => {
  const lines = wordLines(1024, 768);
  assert.equal(lines.length, WEBCORE.word.text.length);
  lines.forEach((l, i) => {
    assert.equal(l.locked, true);
    assert.equal(l.fillColor, AQUA.word.colors[i]);
    assert.equal(l.color, AQUA.word.stroke);
    assert.equal(l.points.length, WEBCORE.word.glyphs[WEBCORE.word.text[i]].length);
    for (const p of l.points) assert.ok(p.x >= 0 && p.x <= 1024 && p.y >= 0 && p.y <= 768);
  });
});
