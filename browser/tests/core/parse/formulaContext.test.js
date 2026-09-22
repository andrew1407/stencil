import { test } from 'node:test';
import assert from 'node:assert';
import { FormulaEngine } from '../../../js/core/parse/formulaEngine.js';

// Mirrors core/tests/parse/formulaContext.test.cpp.

const fe = new FormulaEngine();

// A4 portrait behind a 600x400 image, with the other axis already resolved.
const a4 = (unit = 'cm') => ({
  x: 10, y: 4, pageWidthCm: 21, pageHeightCm: 29.7,
  imageWidth: 600, imageHeight: 400, unit,
});

const near = (got, want, label) => assert.ok(Math.abs(got - want) < 1e-9, `${label}: ${got} != ${want}`);

test('the named constants, both axes and a bare number all evaluate', () => {
  // [expr, the axis `value` binds, value, expected]
  const cases = [
    ['PAGE_WIDTH', 'x', 0, 21],
    ['PAGE_HEIGHT', 'x', 0, 29.7],
    ['PAGE_WIDTH_CM', 'x', 0, 21],
    ['PAGE_HEIGHT_CM', 'x', 0, 29.7],
    ['PAGE_WIDTH_IN', 'x', 0, 21 / 2.54],
    ['PAGE_HEIGHT_IN', 'x', 0, 29.7 / 2.54],
    ['IMAGE_WIDTH', 'x', 0, 600],
    ['IMAGE_HEIGHT', 'y', 0, 400],
    ['9', 'x', 5, 9],                       // no variable at all
    ['IMAGE_HEIGHT', 'x', 5, 400],          // a constant alone
    ['x / y', 'x', 10, 2.5],                // f(x) reads y
    ['x - 3', 'y', 99, 7],                  // f(y) reads x
    ['PAGE_WIDTH + PAGE_HEIGHT - x / 2', 'x', 8, 46.7],
    ['IMAGE_WIDTH / PAGE_WIDTH', 'x', 0, 600 / 21],
  ];
  for (const [expr, axis, value, expected] of cases) {
    near(fe.applyCtx(expr, axis, value, true, a4()), expected, expr);
    assert.strictEqual(fe.validateCtx(expr, a4()), true, expr);
  }
});

test('PAGE_WIDTH / PAGE_HEIGHT follow the selected unit; the suffixed forms do not', () => {
  near(fe.applyCtx('PAGE_WIDTH', 'x', 0, true, a4('cm')), 21, 'cm');
  near(fe.applyCtx('PAGE_WIDTH', 'x', 0, true, a4('in')), 21 / 2.54, 'in');
  near(fe.applyCtx('PAGE_HEIGHT', 'x', 0, true, a4('in')), 29.7 / 2.54, 'in');
  near(fe.applyCtx('PAGE_HEIGHT_CM', 'x', 0, true, a4('in')), 29.7, '_CM under in');
  near(fe.applyCtx('PAGE_WIDTH_IN', 'x', 0, true, a4('cm')), 21 / 2.54, '_IN under cm');
});

test('names are case-sensitive and matched whole', () => {
  for (const expr of ['PAGE_WIDTHS', 'page_width', 'Page_Width', 'PAGE', 'X', 'Y',
    'PAGE_WIDTH_MM', 'IMAGE_WIDTH2', 'foo']) {
    assert.strictEqual(fe.validateCtx(expr, a4()), false, expr);
    assert.strictEqual(fe.applyCtx(expr, 'x', 42, true, a4()), 42, expr);
  }
});

test('a constant the caller did not supply is invalid, never zero', () => {
  const blank = { x: 3 };   // nothing open: no page, no image
  assert.strictEqual(fe.validateCtx('IMAGE_WIDTH', blank), false);
  assert.strictEqual(fe.validateCtx('PAGE_WIDTH', blank), false);
  assert.strictEqual(fe.applyCtx('IMAGE_WIDTH * 2', 'x', 7, true, blank), 7);
  assert.strictEqual(fe.applyCtx('x * 2', 'x', 7, true, blank), 14);
});

test('validate probes an unbound axis at 1, so a cross-axis formula passes', () => {
  const ctx = { ...a4(), x: undefined, y: undefined };
  assert.strictEqual(fe.validateCtx('x / y', ctx), true);
  assert.strictEqual(fe.validateCtx('x - 3', ctx), true);
  assert.strictEqual(fe.validateCtx('', ctx), true);
  assert.strictEqual(fe.validateCtx('   ', ctx), true);
  assert.strictEqual(fe.applyCtx('  ', 'x', 5, true, ctx), 5);
  assert.strictEqual(fe.applyCtx('PAGE_WIDTH', 'x', 5, false, ctx), 5);
});

test('division by zero and the depth cap are unchanged by a context', () => {
  const ctx = { ...a4(), y: 0 };
  assert.strictEqual(fe.validateCtx('x / y', ctx), false);
  assert.strictEqual(fe.applyCtx('x / y', 'x', 5, true, ctx), 5);
  const balanced = '('.repeat(5000) + 'PAGE_WIDTH' + ')'.repeat(5000);
  assert.strictEqual(fe.validateCtx(balanced, a4()), false);
  assert.strictEqual(fe.applyCtx(balanced, 'x', 42, true, a4()), 42);
});

test('the context-free entry points behave exactly as before', () => {
  assert.strictEqual(fe.validate('PAGE_WIDTH', 'x'), false);
  assert.strictEqual(fe.apply('PAGE_WIDTH', 'x', 5, true), 5);
  assert.strictEqual(fe.validate('y', 'x'), false);
  assert.strictEqual(fe.apply('x * 2', 'x', 5, true), 10);
});
