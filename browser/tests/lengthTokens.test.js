import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseLengthToken, resolveAxisPx, normalizePageSize, isDeltaToken } from '../js/core/units.js';

// Pure length-token parsing used by the console API (window.stencil) for crop/move.

test('parseLengthToken: numbers are px deltas (sign preserved)', () => {
  assert.deepEqual(parseLengthToken(10), { kind: 'delta', value: 10 });
  assert.deepEqual(parseLengthToken(-4), { kind: 'delta', value: -4 });
  assert.equal(parseLengthToken(NaN), null);
  assert.deepEqual(parseLengthToken('12'), { kind: 'delta', value: 12 });
  assert.deepEqual(parseLengthToken('-7'), { kind: 'delta', value: -7 });
});

test('parseLengthToken: units → cm, with from-end flag on leading minus', () => {
  assert.deepEqual(parseLengthToken('3cm'), { kind: 'cm', value: 3, fromEnd: false });
  assert.deepEqual(parseLengthToken('-4cm'), { kind: 'cm', value: 4, fromEnd: true });
  assert.deepEqual(parseLengthToken('1in'), { kind: 'cm', value: 2.54, fromEnd: false });
  assert.deepEqual(parseLengthToken('20mm'), { kind: 'cm', value: 2, fromEnd: false });
  assert.deepEqual(parseLengthToken('50px'), { kind: 'px', value: 50, fromEnd: false });
});

test('parseLengthToken: percent + bad input', () => {
  assert.deepEqual(parseLengthToken('50%'), { kind: 'percent', value: 50, fromEnd: false });
  assert.deepEqual(parseLengthToken('-60%'), { kind: 'percent', value: 60, fromEnd: true });
  assert.equal(parseLengthToken('abc'), null);
  assert.equal(parseLengthToken(''), null);
  assert.equal(parseLengthToken(null), null);
});

test('resolveAxisPx: deltas add to current; absolutes from start/end', () => {
  const ctx = { lengthPx: 1000, pxPerCm: 10, currentPx: 100 };
  assert.equal(resolveAxisPx(25, ctx), 125);              // +25 px from current
  assert.equal(resolveAxisPx('200px', ctx), 200);         // absolute px
  assert.equal(resolveAxisPx('5cm', ctx), 50);            // 5cm * 10 px/cm
  assert.equal(resolveAxisPx('-5cm', ctx), 950);          // from end: 1000 - 50
  assert.equal(resolveAxisPx('50%', ctx), 500);           // half the axis
  assert.equal(resolveAxisPx('-60%', ctx), 400);          // 1000 - 600
  assert.equal(resolveAxisPx('nope', ctx), null);
});

test('normalizePageSize: case-insensitive over the whole ISO table, else null', () => {
  assert.equal(normalizePageSize('a3'), 'A3');
  assert.equal(normalizePageSize('A4'), 'A4');
  assert.equal(normalizePageSize('B5'), 'B5');
  assert.equal(normalizePageSize('a10'), 'A10');
  assert.equal(normalizePageSize('c10'), 'C10');
  assert.equal(normalizePageSize('Custom'), 'custom');
  assert.equal(normalizePageSize('D4'), null);
  assert.equal(normalizePageSize(''), null);
});

test('isDeltaToken: only bare numbers are deltas', () => {
  assert.equal(isDeltaToken(5), true);
  assert.equal(isDeltaToken('5'), true);
  assert.equal(isDeltaToken('5cm'), false);
  assert.equal(isDeltaToken('50%'), false);
});
