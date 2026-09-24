// The webcore picture's cells and the word's lines (js/ui/webcore/rules.js): integer cells, so the
// desktop twin (support/webcore/rules.cpp) lands on the same colours; the sample values here are
// what it pins.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  WEBCORE, cellGrid, hillTopCell, skyBandAt, grassShadeAt, cellColorAt, cloudBlocks, cellRuns,
  pixelColorAt, wordLines, OFF_TOAST, PROJECT_NAME, IMAGE_NAME,
} from '../../../js/ui/webcore/rules.js';

test('the grid is the picture in cells, the horizon a share of its rows', () => {
  const g = cellGrid();
  assert.deepEqual(g, { cell: 4, cols: 256, rows: 192, horizon: 111 });
});

test('the hill is a repeating parabola: its foot on the horizon, its crest ampCells up', () => {
  const g = cellGrid();
  const tops = Array.from({ length: g.cols }, (_, cx) => hillTopCell(cx, g));
  assert.equal(Math.max(...tops), g.horizon, 'never below the horizon');
  assert.equal(Math.min(...tops), g.horizon - WEBCORE.image.hill.ampCells, 'up to the amplitude');
  assert.equal(hillTopCell(0, g), 103, 'printed for the desktop');
  assert.equal(hillTopCell(60, g), 97);
  assert.equal(hillTopCell(160, g), 111);
  assert.equal(hillTopCell(255, g), 98);
});

test('the sky bands run top to bottom, the grass crest to foot with a dither at each edge', () => {
  const g = cellGrid();
  assert.equal(skyBandAt(0, g), WEBCORE.image.skyBands[0]);
  assert.equal(skyBandAt(g.horizon - 1, g), WEBCORE.image.skyBands.at(-1));
  assert.equal(grassShadeAt(0, hillTopCell(0, g), g), WEBCORE.image.grass[0]);
  assert.equal(grassShadeAt(0, g.rows - 1, g), WEBCORE.image.grass.at(-1));
  // A boundary row alternates with its neighbour, cell by cell.
  const seen = new Set();
  for (let cx = 0; cx < 8; cx++) seen.add(grassShadeAt(cx, hillTopCell(cx, g) + 18, g));
  assert.equal(seen.size, 2, 'checker dither');
  assert.equal(cellColorAt(0, 0, g), '#0000a8');
  assert.equal(cellColorAt(128, 60, g), '#4898f0');
  assert.equal(cellColorAt(30, 100, g), '#5cc85c');
  assert.equal(cellColorAt(31, 100, g), '#5cc85c');
  assert.equal(cellColorAt(200, 150, g), '#149614');
  assert.equal(cellColorAt(100, 120, g), '#5cc85c');
  assert.equal(cellColorAt(101, 120, g), '#30b030', 'the dither\'s other cell');
});

test('clouds are blocks over the sky, and the runs cover every cell exactly once', () => {
  const g = cellGrid();
  const blocks = cloudBlocks();
  assert.equal(blocks.length, 18);
  assert.deepEqual(blocks[0], { x: 28, y: 26, w: 26, h: 6, color: '#ffffff' });
  assert.equal(blocks.at(-1).color, '#c0c0c0');
  const runs = cellRuns(g);
  assert.equal(runs.reduce((n, r) => n + r.w * r.h, 0), g.cols * g.rows);
  assert.equal(runs[0].w, g.cols, 'the top row is one band');
  assert.equal(pixelColorAt(0, 0), '#0000a8');
  assert.equal(pixelColorAt(160, 120), '#ffffff', 'inside the first cloud');
  assert.equal(pixelColorAt(160, 140), '#0050d8', 'below it, the sky again');
  assert.equal(pixelColorAt(512, 767), '#0a7a0a');
  assert.equal(pixelColorAt(1023, 300), '#70b8f8');
  assert.equal(pixelColorAt(400, 600), '#149614');
});

test('the word is seven locked, filled lines laid across the sky', () => {
  const lines = wordLines(1024, 768);
  assert.equal(lines.length, 7);
  const colors = new Set();
  for (const l of lines) {
    assert.equal(l.locked, true);
    assert.equal(l.color, '#000000');
    assert.equal(l.thickness, 2);
    colors.add(l.fillColor);
    for (const p of l.points) assert.ok(p.y < 768 * 0.58 && p.x >= 0 && p.x <= 1024, 'in the sky');
  }
  assert.equal(colors.size, 7);
  assert.deepEqual(lines[0].points[0], { x: 102.4, y: 154.04 }, 'S starts at the top-left cell');
  assert.deepEqual(lines[0].points[6], { x: 189.55, y: 276.04 });
  assert.deepEqual(lines[6].points[3], { x: 921.6, y: 258.61 }, 'L\'s foot');
  assert.equal(lines[3].points.length, 10, 'N is the one with diagonals');
  assert.equal(OFF_TOAST, WEBCORE.strings.off);
  assert.equal(PROJECT_NAME, 'webcore');
  assert.equal(IMAGE_NAME, 'webcore.png');
});
