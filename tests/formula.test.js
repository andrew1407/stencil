import { test } from 'node:test';
import assert from 'node:assert';
import { FormulaEngine } from '../js/core/formulaEngine.js';

const fe = new FormulaEngine();

test('validate empty / whitespace = true (identity)', () => {
    assert.strictEqual(fe.validate('', 'x'), true);
    assert.strictEqual(fe.validate('  ', 'x'), true);
});

test('validate valid expression true', () => {
    assert.strictEqual(fe.validate('x*2', 'x'), true);
});

test('validate syntax error false', () => {
    assert.strictEqual(fe.validate('x+', 'x'), false);
});

test('validate ReferenceError (unknown fn) false', () => {
    assert.strictEqual(fe.validate('foo(x)', 'x'), false);
});

test('validate non-finite (1/0) false', () => {
    assert.strictEqual(fe.validate('1/0', 'x'), false);
});

test('apply with allowFormulas true', () => {
    assert.strictEqual(fe.apply('x*2', 'x', 5, true), 10);
});

test('apply with allowFormulas false → identity', () => {
    assert.strictEqual(fe.apply('x*2', 'x', 5, false), 5);
});

test('apply with empty expr → identity', () => {
    assert.strictEqual(fe.apply('', 'x', 5, true), 5);
});

test('apply with invalid expr → original value', () => {
    assert.strictEqual(fe.apply('x+', 'x', 5, true), 5);
});
