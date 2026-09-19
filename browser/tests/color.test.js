import { test } from 'node:test';
import assert from 'node:assert';
import { hexToRgba, parseHex, cssColorParts, cssWithAlpha } from '../js/utils.js';

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

// <input type="color"> cannot carry an alpha byte, so the editors keep the opacity in a slider beside the
// swatch and these two put the halves together. Desktop twin: desktop/src/support/cssColor.hpp.
test('cssColorParts splits a stored colour into swatch + opacity', () => {
    assert.deepStrictEqual(cssColorParts('#ff8800'), { hex: '#ff8800', alpha: 1 });
    assert.deepStrictEqual(cssColorParts('#FF8800'), { hex: '#ff8800', alpha: 1 });
    const half = cssColorParts('#ff880080');
    assert.equal(half.hex, '#ff8800');
    assert.ok(Math.abs(half.alpha - 128 / 255) < 1e-9, 'the alpha byte is the opacity');
    assert.deepStrictEqual(cssColorParts('#ff880000'), { hex: '#ff8800', alpha: 0 });
    // Anything that is not hex keeps its value and reads opaque, so a control fed a CSS
    // name or 'transparent' still shows something sensible.
    assert.deepStrictEqual(cssColorParts('transparent'), { hex: 'transparent', alpha: 1 });
    assert.deepStrictEqual(cssColorParts(null), { hex: '', alpha: 1 });
});

test('cssWithAlpha only spends the extra byte when there is alpha to carry', () => {
    assert.equal(cssWithAlpha('#ff8800', 1), '#ff8800', 'opaque stays the form every surface reads');
    assert.equal(cssWithAlpha('#ff8800', 0.5), '#ff880080');
    assert.equal(cssWithAlpha('#ff8800', 0), '#ff880000');
    assert.equal(cssWithAlpha('#FF8800', 0.5), '#ff880080', 'case-normalised');
    assert.equal(cssWithAlpha('#ff8800', 5), '#ff8800', 'clamped high');
    assert.equal(cssWithAlpha('transparent', 0.5), 'transparent', 'a non-hex value passes through');
});

test('the two round-trip, so re-opening the panel shows what was stored', () => {
    for (const css of ['#ff8800', '#00000000', '#123456ab', '#ffffff01']) {
        const { hex, alpha } = cssColorParts(css);
        assert.equal(cssWithAlpha(hex, alpha), css.toLowerCase(), css);
    }
});
