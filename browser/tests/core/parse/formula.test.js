import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { FormulaEngine, FORMULA_MAX_CHARS, formulaText } from '../../../js/core/parse/formulaEngine.js';

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

// Mirrors core/tests/parse/formula.test.cpp: untrusted formulas (layout JSON, the console facade, server co-edit)
// must never blow the stack or hang, and past the recursion cap are invalid — MAX_DEPTH in lockstep with core.
test('deeply nested parens are invalid (identity), not a stack overflow', () => {
    assert.strictEqual(fe.validate('('.repeat(200000), 'x'), false);
    const balanced = '('.repeat(5000) + 'x' + ')'.repeat(5000);
    assert.strictEqual(fe.validate(balanced, 'x'), false);
    assert.strictEqual(fe.apply(balanced, 'x', 42, true), 42);
    assert.strictEqual(fe.validate('-'.repeat(200000) + 'x', 'x'), false);
});

test('past the recursion cap is invalid within the length cap too', () => {
    const over = '('.repeat(300) + 'x' + ')'.repeat(300);
    assert.ok(over.length <= FORMULA_MAX_CHARS);
    assert.strictEqual(fe.validate(over, 'x'), false);
    assert.strictEqual(fe.validate('('.repeat(100) + 'x' + ')'.repeat(100), 'x'), true);
});

test('a long flat expression stays linear and valid', () => {
    const flat = '0' + '+1'.repeat(499);
    assert.strictEqual(flat.length, 999);
    assert.strictEqual(fe.apply(flat, 'x', 0, true), 499);
});

// Core parses no JSON, so MAX_CHARS is pinned in its header; this guards that literal.
test('the length cap is one value: constants.json, the JS twin and the core header', () => {
    assert.strictEqual(FORMULA_MAX_CHARS, constants.LIMITS.formulaMaxChars);
    const hpp = readFileSync(new URL('../../../../core/parse/formulaParser.hpp', import.meta.url), 'utf8');
    assert.strictEqual(Number(/MAX_CHARS\s*=\s*(\d+)/.exec(hpp)?.[1]), FORMULA_MAX_CHARS);
});

test('an expression past the cap is invalid (identity), however simple; a blank one is identity', () => {
    const at = 'x' + ' '.repeat(FORMULA_MAX_CHARS - 1);
    assert.strictEqual(fe.validate(at, 'x'), true);
    assert.strictEqual(fe.validate(at + ' ', 'x'), false);
    assert.strictEqual(fe.apply(at + ' ', 'x', 7, true), 7);
    assert.strictEqual(fe.applyCtx('1' + ' '.repeat(FORMULA_MAX_CHARS), 'x', 7, true, {}), 7);
    assert.strictEqual(fe.validate(' '.repeat(5000), 'x'), true);
});

test('formulaText keeps a stored formula within the cap and drops anything else', () => {
    const at = 'x'.repeat(FORMULA_MAX_CHARS);
    assert.strictEqual(formulaText('x*2'), 'x*2');
    assert.strictEqual(formulaText(at), at);
    assert.strictEqual(formulaText(at + 'x'), '');
    assert.strictEqual(formulaText(undefined), '');
    assert.strictEqual(formulaText(42), '');
});

test('numeric overflow yields invalid (identity)', () => {
    assert.strictEqual(fe.validate('9e999', 'x'), false);
    assert.strictEqual(fe.validate('1e308*1e308', 'x'), false);
    assert.strictEqual(fe.validate('2**2**2**2**2', 'x'), false);
    assert.strictEqual(fe.apply('1e308*1e308', 'x', 7, true), 7);
});

// The core's wasm build has no exceptions, so these once aborted it; both sides read them alike.
test('the inputs parseFloat reads differently from std::stod parse as parseFloat does', () => {
    assert.strictEqual(fe.validate('.', 'x'), false);
    assert.strictEqual(fe.validate('x*1e999', 'x'), false);
    assert.strictEqual(fe.apply('1e-400', 'x', 5, true), 0);
    assert.strictEqual(fe.apply('1.2.3', 'x', 5, true), 1.2);
    assert.strictEqual(fe.validate('1**(1/0)', 'x'), false);
    assert.strictEqual(fe.apply('0.5**(1/0)', 'x', 5, true), 0);
    assert.strictEqual(fe.validate('\u00a0x', 'x'), false); // ASCII whitespace only
    assert.strictEqual(fe.validate('\u00a0', 'x'), false);
    assert.strictEqual(fe.validate(' \t\v\f\r\n', 'x'), true);
});
