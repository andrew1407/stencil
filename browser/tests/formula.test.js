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

// Security/robustness — mirrors core/tests/formula.test.cpp. Untrusted formulas
// (layout JSON, the console facade, server co-edit) must never blow the stack or
// hang; past the recursion cap they're invalid (→ identity). Kept in lockstep with
// the core parser's kMaxDepth so wasm and this JS fallback agree.
test('deeply nested parens are invalid (identity), not a stack overflow', () => {
    assert.strictEqual(fe.validate('('.repeat(200000), 'x'), false);
    const balanced = '('.repeat(5000) + 'x' + ')'.repeat(5000);
    assert.strictEqual(fe.validate(balanced, 'x'), false);
    assert.strictEqual(fe.apply(balanced, 'x', 42, true), 42);
    assert.strictEqual(fe.validate('-'.repeat(200000) + 'x', 'x'), false);
});

test('a long flat expression stays linear and valid', () => {
    const flat = '0' + '+1'.repeat(20000);
    assert.strictEqual(fe.apply(flat, 'x', 0, true), 20000);
});

test('numeric overflow yields invalid (identity)', () => {
    assert.strictEqual(fe.validate('9e999', 'x'), false);
    assert.strictEqual(fe.validate('1e308*1e308', 'x'), false);
    assert.strictEqual(fe.validate('2**2**2**2**2', 'x'), false);
    assert.strictEqual(fe.apply('1e308*1e308', 'x', 7, true), 7);
});
