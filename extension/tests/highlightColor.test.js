// Tests for the highlight-colour resolver (src/lib/highlightColor.js): 'theme' follows
// the main accent, a hex is used verbatim, and an unknown accent falls back to the default.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { resolveHighlightColor, ACCENT_HEX, DEFAULT_HL } from '../src/lib/highlightColor.js';

test('resolveHighlightColor: "theme" maps to the accent hex', () => {
  assert.equal(resolveHighlightColor('theme', 'pink'), ACCENT_HEX.pink);
  assert.equal(resolveHighlightColor('theme', 'sky'), '#0ea5e9');
  assert.equal(resolveHighlightColor('theme', 'violet'), DEFAULT_HL);
});

test('resolveHighlightColor: empty/undefined setting is treated as "theme"', () => {
  assert.equal(resolveHighlightColor('', 'blue'), ACCENT_HEX.blue);
  assert.equal(resolveHighlightColor(undefined, 'grass'), ACCENT_HEX.grass);
});

test('resolveHighlightColor: an unknown accent key falls back to the default', () => {
  assert.equal(resolveHighlightColor('theme', 'no-such-accent'), DEFAULT_HL);
  assert.equal(resolveHighlightColor('theme', undefined), DEFAULT_HL);
});

test('resolveHighlightColor: a custom hex wins over the accent', () => {
  assert.equal(resolveHighlightColor('#ff0000', 'pink'), '#ff0000');
  assert.equal(resolveHighlightColor('#0a0a0a', 'violet'), '#0a0a0a');
});
