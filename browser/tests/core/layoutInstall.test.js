// js/core/layoutInstall.js installLayout: a combine joins the drawn lines and the new ones, and the
// join is cut at the total-points cap as one layout, so repeating it cannot grow the history past it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../helpers/dom.js';

installDom();
const { installLayout } = await import('../../js/core/layoutInstall.js');

const pts = (n) => Array.from({ length: n }, (_, i) => ({ x: i, y: 0 }));
const appWith = (lines) => {
  const app = {
    image: {}, canvas: { width: 4, height: 3 }, lines, saves: 0,
    saveHistory() { app.saves++; }, renderer: { redraw() {} }, coordTable: { update() {} },
  };
  return app;
};

test('a combine that passes the total-points cap is cut there, the drawn lines first', () => {
  const app = appWith([{ points: pts(600_000) }]);
  const layout = { imageWidth: 4, imageHeight: 3, lines: [{ points: pts(100_000) }, ...Array(5).fill({ points: pts(100_000) })] };
  assert.ok(installLayout(app, layout, { mode: 'combine' }));
  assert.deepEqual(app.lines.map((l) => l.points.length), [600_000, 100_000, 100_000, 100_000, 100_000]);
  assert.ok(installLayout(app, layout, { mode: 'combine' }), 'a second combine is a no-op past the cap');
  assert.equal(app.lines.reduce((n, l) => n + l.points.length, 0), 1_000_000);
  assert.equal(app.saves, 2);
});

test('a combine under the caps keeps every line, a replace takes only the new ones', () => {
  const drawn = { points: pts(2) };
  const app = appWith([drawn]);
  installLayout(app, { imageWidth: 4, imageHeight: 3, lines: [{ points: pts(3) }] }, { mode: 'combine' });
  assert.equal(app.lines[0], drawn);
  assert.deepEqual(app.lines.map((l) => l.points.length), [2, 3]);
  installLayout(app, { imageWidth: 4, imageHeight: 3, lines: [{ points: pts(1) }] });
  assert.deepEqual(app.lines.map((l) => l.points.length), [1]);
});
