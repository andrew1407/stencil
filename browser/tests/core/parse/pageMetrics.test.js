import { test } from 'node:test';
import assert from 'node:assert';
import { formulaContext, getPageDimensions, pixelToPageCoords, pixelToPageCoordsAll } from '../../../js/core/parse/pageMetrics.js';
import { FormulaEngine } from '../../../js/core/parse/formulaEngine.js';

// A4 under a landscape 600x400 image, so getPageDimensions swaps to {29.7, 21}.
const makeApp = (over = {}) => ({
  pageSize: 'A4', customPageWidth: 21, customPageHeight: 29.7,
  canvas: { width: 600, height: 400 }, image: {}, unit: 'cm',
  allowFormulas: true, formulaX: '', formulaY: '',
  formula: new FormulaEngine(), ...over,
});

const near = (got, want, label) => assert.ok(Math.abs(got - want) < 1e-9, `${label}: ${got} != ${want}`);

test('formulaContext carries the page in cm, the image in px and the display unit', () => {
  const ctx = formulaContext(makeApp());
  assert.deepStrictEqual(ctx, {
    pageWidthCm: 29.7, pageHeightCm: 21, imageWidth: 600, imageHeight: 400, unit: 'cm',
  });
});

test('with no image open the image names stay unsupplied', () => {
  const app = makeApp({ image: null, formulaX: 'IMAGE_WIDTH' });
  assert.strictEqual(formulaContext(app).imageWidth, undefined);
  assert.strictEqual(app.formula.validateCtx('IMAGE_WIDTH', formulaContext(app)), false);
  near(pixelToPageCoords(app, 300, 200).x, 14.85, 'identity fallback');
});

test('either formula may read both axes and the named constants', () => {
  const ps = getPageDimensions(makeApp());
  assert.deepStrictEqual(ps, { width: 29.7, height: 21 });
  const app = makeApp({ formulaX: 'x / y', formulaY: 'PAGE_WIDTH - y' });
  const p = pixelToPageCoords(app, 300, 200);   // raw = (14.85, 10.5)
  near(p.x, 14.85 / 10.5, 'f(x) reads y');
  near(p.y, 29.7 - 10.5, 'f(y) reads PAGE_WIDTH');
});

test('a constant-only formula needs no variable, and inches follow the unit', () => {
  near(pixelToPageCoords(makeApp({ formulaX: '9' }), 300, 200).x, 9, 'constant only');
  near(pixelToPageCoords(makeApp({ formulaX: 'IMAGE_HEIGHT' }), 300, 200).x, 400, 'image px');
  near(pixelToPageCoords(makeApp({ formulaX: 'PAGE_WIDTH', unit: 'in' }), 300, 200).x,
    29.7 / 2.54, 'selected unit');
  near(pixelToPageCoords(makeApp({ formulaX: 'PAGE_WIDTH_CM', unit: 'in' }), 300, 200).x,
    29.7, 'explicit cm');
});

test('formulas off, and an invalid formula, both leave the raw page coordinate', () => {
  near(pixelToPageCoords(makeApp({ formulaX: '9', allowFormulas: false }), 300, 200).x, 14.85, 'off');
  near(pixelToPageCoords(makeApp({ formulaX: 'PAGE_WIDTHS' }), 300, 200).x, 14.85, 'unknown name');
});

// BR-4: hover asks per move, so the identity case never reaches the formula engine (wasm or
// JS), and the page size is computed once per (page, canvas size).
test('formulas off or blank never dispatch to the engine', () => {
  const formula = { applyCtx() { throw new Error('dispatched'); } };
  for (const over of [{ allowFormulas: false, formulaX: 'x*2' }, { formulaX: '', formulaY: '  ' }]) {
    const p = pixelToPageCoords(makeApp({ ...over, formula }), 300, 200);
    near(p.x, 14.85, 'raw x');
    near(p.y, 10.5, 'raw y');
  }
  const calls = [];
  const one = { applyCtx: (expr, axis, v) => { calls.push(axis); return v * 2; } };
  const p = pixelToPageCoords(makeApp({ formulaY: 'y*2', formula: one }), 300, 200);
  assert.deepStrictEqual(calls, ['y'], 'only the axis with a formula is evaluated');
  near(p.x, 14.85, 'x stays raw');
  near(p.y, 21, 'y doubled');
});

test('page dimensions are cached per page and canvas size, and handed out as copies', () => {
  const app = makeApp();
  const a = getPageDimensions(app);
  a.width = -1;
  assert.deepStrictEqual(getPageDimensions(app), { width: 29.7, height: 21 }, 'a caller cannot poison the cache');
  app.canvas = { width: 400, height: 600 };
  assert.deepStrictEqual(getPageDimensions(app), { width: 21, height: 29.7 }, 'a canvas resize recomputes');
  app.pageSize = 'custom';
  app.customPageWidth = 10;
  assert.deepStrictEqual(getPageDimensions(app), { width: 10, height: 29.7 }, 'a page change recomputes');
});

test('every point at once answers as pixelToPageCoords does point by point', () => {
  const points = [{ x: 300, y: 200 }, { x: 0, y: 0 }, { x: 599, y: 13 }];
  for (const over of [{}, { formulaX: 'x / y', formulaY: 'PAGE_WIDTH - y' }, { formulaY: 'y*2' },
    { formulaX: 'PAGE_WIDTHS' }, { formulaX: 'x*2', allowFormulas: false }]) {
    const app = makeApp(over);
    const all = pixelToPageCoordsAll(app, points);
    points.forEach((p, i) => {
      const one = pixelToPageCoords(app, p.x, p.y);
      near(all[i].x, one.x, `${JSON.stringify(over)} x${i}`);
      near(all[i].y, one.y, `${JSON.stringify(over)} y${i}`);
    });
  }
  const calls = [];
  const one = { applyCtx: (expr, axis, v) => { calls.push(axis); return v; } };
  pixelToPageCoordsAll(makeApp({ formulaY: 'y*2', formula: one }), points);
  assert.deepStrictEqual(calls, ['y', 'y', 'y'], 'only the axis with a formula is evaluated');
});
