import { test } from 'node:test';
import assert from 'node:assert';
import { hexToRgba, parseHex } from '../js/utils.js';

test('#FF0000 with alpha 0.5 → rgba(255,0,0,0.5)', () => {
    assert.strictEqual(hexToRgba('#FF0000', 0.5), 'rgba(255,0,0,0.5)');
});

test('lowercase #00ff00 with alpha 1 → rgba(0,255,0,1)', () => {
    assert.strictEqual(hexToRgba('#00ff00', 1), 'rgba(0,255,0,1)');
});

test('pass-through for named color "transparent"', () => {
    assert.strictEqual(hexToRgba('transparent', 0.5), 'transparent');
});

test('pass-through for existing rgba string', () => {
    assert.strictEqual(hexToRgba('rgba(1,2,3,0.4)', 0.5), 'rgba(1,2,3,0.4)');
});

test('pass-through for short hex', () => {
    assert.strictEqual(hexToRgba('#fff', 0.5), '#fff');
});

test('pass-through for non-string', () => {
    assert.strictEqual(hexToRgba(123, 0.5), 123);
});

test('parseHex(#3399ff) → {r:51,g:153,b:255}', () => {
    assert.deepStrictEqual(parseHex('#3399ff'), { r: 51, g: 153, b: 255 });
});
