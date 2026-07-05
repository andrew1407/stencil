import { test } from 'node:test';
import assert from 'node:assert';
import { parseDuration } from '../js/core/durationParser.js';

// Mirrors core/tests/durationParser.test.cpp. node --test runs the JS fallback;
// the wasm build is checked against it by wasm-parity.test.js.

const DAY = 24 * 60 * 60 * 1000;

test('bare units mean one of them', () => {
    assert.strictEqual(parseDuration('day'), DAY);
    assert.strictEqual(parseDuration('week'), 7 * DAY);
    assert.strictEqual(parseDuration('fortnight'), 14 * DAY);
    assert.strictEqual(parseDuration('month'), 30 * DAY);
    assert.strictEqual(parseDuration('year'), 365 * DAY);
});

test('count then unit', () => {
    assert.strictEqual(parseDuration('days 1'), DAY);
    assert.strictEqual(parseDuration('days 23'), 23 * DAY);
    assert.strictEqual(parseDuration('months 3'), 3 * 30 * DAY);
});

test('unit then count (either order)', () => {
    assert.strictEqual(parseDuration('3 months'), 3 * 30 * DAY);
    assert.strictEqual(parseDuration('2 fortnights'), 28 * DAY);
});

test('plural and singular both accepted', () => {
    assert.strictEqual(parseDuration('day 2'), 2 * DAY);
    assert.strictEqual(parseDuration('days 2'), 2 * DAY);
    assert.strictEqual(parseDuration('days'), DAY); // bare plural = one
});

test('case-insensitive and whitespace tolerant', () => {
    assert.strictEqual(parseDuration('  MONTHS   3 '), 3 * 30 * DAY);
    assert.strictEqual(parseDuration('Fortnight'), 14 * DAY);
});

test('off / never / none = keep forever (0)', () => {
    assert.strictEqual(parseDuration('off'), 0);
    assert.strictEqual(parseDuration('never'), 0);
    assert.strictEqual(parseDuration('none'), 0);
});

test('invalid specs return null', () => {
    assert.strictEqual(parseDuration(''), null);
    assert.strictEqual(parseDuration('   '), null);
    assert.strictEqual(parseDuration('banana'), null);
    assert.strictEqual(parseDuration('days 0'), null);   // non-positive count
    assert.strictEqual(parseDuration('days -3'), null);  // sign is not a digit
    assert.strictEqual(parseDuration('days 2.5'), null); // non-integer
    assert.strictEqual(parseDuration('3 days 2'), null); // too many tokens
    assert.strictEqual(parseDuration('3 weeks banana'), null);
});

test('overflow returns null', () => {
    assert.strictEqual(parseDuration('days 99999999999999999999'), null);
    assert.strictEqual(parseDuration('years 100000000000000000'), null);
});

test('product cap is Number.MAX_SAFE_INTEGER (parity with C++)', () => {
    assert.strictEqual(parseDuration('days 100000000'), 100000000 * DAY); // fits in 2^53-1
    assert.strictEqual(parseDuration('days 200000000'), null);            // exceeds it
});
