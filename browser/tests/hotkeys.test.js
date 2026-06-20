import { test } from 'node:test';
import assert from 'node:assert';
import { normalizeKey, parseHotkey, matchHotkey, comboFromEvent, isTypingTarget,
    isMacPlatform, platformizeCombo, formatCombo } from '../js/utils.js';

test('normalizeKey strips Key/Digit prefixes, passes others', () => {
    assert.strictEqual(normalizeKey('KeyA', 'a'), 'A');
    assert.strictEqual(normalizeKey('Digit0', '0'), '0');
    assert.strictEqual(normalizeKey('ArrowUp', 'ArrowUp'), 'ArrowUp');
    assert.strictEqual(normalizeKey('', 'x'), 'x');
});

test('parseHotkey parses modifiers + key', () => {
    assert.deepStrictEqual(parseHotkey('Ctrl+Shift+Z'),
        { ctrl: true, shift: true, alt: false, meta: false, key: 'Z' });
    assert.strictEqual(parseHotkey(''), null);
    assert.strictEqual(parseHotkey('Alt+ArrowUp').key, 'ArrowUp');
});

test('matchHotkey true when ctrlKey + code KeyC', () => {
    assert.strictEqual(matchHotkey({ ctrlKey: true, code: 'KeyC', key: 'c' }, 'Ctrl+C'), true);
});

test('matchHotkey false when extra modifier present', () => {
    assert.strictEqual(matchHotkey({ ctrlKey: true, shiftKey: true, code: 'KeyC', key: 'c' }, 'Ctrl+C'), false);
});

test('matchHotkey is case-insensitive on key', () => {
    assert.strictEqual(matchHotkey({ ctrlKey: true, code: 'KeyC', key: 'C' }, 'ctrl+c'), true);
});

test('comboFromEvent returns null for pure modifier keys', () => {
    assert.strictEqual(comboFromEvent({ key: 'Control' }), null);
    assert.strictEqual(comboFromEvent({ key: 'Shift' }), null);
    assert.strictEqual(comboFromEvent({ key: 'Alt' }), null);
    assert.strictEqual(comboFromEvent({ key: 'Meta' }), null);
});

test('comboFromEvent builds canonical Ctrl,Alt,Shift,Meta order', () => {
    assert.strictEqual(
        comboFromEvent({ key: 'ArrowUp', altKey: true, shiftKey: true, code: 'ArrowUp' }),
        'Alt+Shift+ArrowUp');
});

// ── Platform detection ──────────────────────────────────────────
test('isMacPlatform true via userAgentData.platform', () => {
    assert.strictEqual(isMacPlatform({ userAgentData: { platform: 'macOS' } }), true);
});
test('isMacPlatform true via navigator.platform MacIntel', () => {
    assert.strictEqual(isMacPlatform({ platform: 'MacIntel' }), true);
});
test('isMacPlatform true via Mac userAgent string', () => {
    assert.strictEqual(isMacPlatform({
        userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36',
    }), true);
});
test('isMacPlatform false for Windows / Linux', () => {
    assert.strictEqual(isMacPlatform({ userAgentData: { platform: 'Windows' } }), false);
    assert.strictEqual(isMacPlatform({ platform: 'Win32' }), false);
    assert.strictEqual(isMacPlatform({ platform: 'Linux x86_64' }), false);
    assert.strictEqual(isMacPlatform({
        userAgent: 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36',
    }), false);
});
test('isMacPlatform false / no-throw for empty / nullish navigator-like', () => {
    // An empty navigator object has no Mac hints → false.
    assert.strictEqual(isMacPlatform({}), false);
    // Explicit nullish args bypass the globalThis.navigator default → false, no throw.
    assert.strictEqual(isMacPlatform(null), false);
    assert.doesNotThrow(() => isMacPlatform(null));
    assert.doesNotThrow(() => isMacPlatform({}));
    // Empty userAgentData/strings must not match.
    assert.strictEqual(isMacPlatform({ platform: '', userAgent: '' }), false);
});

// ── platformizeCombo ────────────────────────────────────────────
test('platformizeCombo swaps Ctrl->Meta on Mac (single + multi modifier)', () => {
    assert.strictEqual(platformizeCombo('Ctrl+Z', true), 'Meta+Z');
    assert.strictEqual(platformizeCombo('Ctrl+Shift+Z', true), 'Meta+Shift+Z');
    assert.strictEqual(platformizeCombo('Ctrl+C', true), 'Meta+C');
});
test('platformizeCombo leaves Alt-only / Shift-only combos alone on Mac', () => {
    assert.strictEqual(platformizeCombo('Alt+J', true), 'Alt+J');
    assert.strictEqual(platformizeCombo('Shift+ArrowUp', true), 'Shift+ArrowUp');
    assert.strictEqual(platformizeCombo('Alt+0', true), 'Alt+0');
});
test('platformizeCombo is a no-op when isMac is false', () => {
    assert.strictEqual(platformizeCombo('Ctrl+Z', false), 'Ctrl+Z');
    assert.strictEqual(platformizeCombo('Ctrl+Shift+Z', false), 'Ctrl+Shift+Z');
});
test('platformizeCombo is idempotent (apply twice == once)', () => {
    const once = platformizeCombo('Ctrl+Shift+Z', true);
    assert.strictEqual(platformizeCombo(once, true), once);
    assert.strictEqual(once, 'Meta+Shift+Z');
});
test('platformizeCombo is case-insensitive on the Ctrl token', () => {
    assert.strictEqual(platformizeCombo('ctrl+z', true), 'Meta+z');
    assert.strictEqual(platformizeCombo('CTRL+Shift+Z', true), 'Meta+Shift+Z');
});
test('platformizeCombo swaps Delete->Backspace on Mac (the ⌫ delete key)', () => {
    assert.strictEqual(platformizeCombo('Alt+Delete', true), 'Alt+Backspace');
    assert.strictEqual(platformizeCombo('Alt+Shift+Delete', true), 'Alt+Shift+Backspace');
    // Unchanged off-Mac.
    assert.strictEqual(platformizeCombo('Alt+Delete', false), 'Alt+Delete');
});

// ── formatCombo (display) ───────────────────────────────────────
test('formatCombo renders Apple symbols on Mac', () => {
    assert.strictEqual(formatCombo('Meta+C', true), '⌘C');
    assert.strictEqual(formatCombo('Meta+Shift+Z', true), '⇧⌘Z');
    assert.strictEqual(formatCombo('Ctrl+Shift+Z', true), '⌃⇧Z');
    assert.strictEqual(formatCombo('Alt+J', true), '⌥J');
});
test('formatCombo renders arrow keys on Mac', () => {
    assert.strictEqual(formatCombo('Alt+ArrowUp', true), '⌥↑');
    assert.strictEqual(formatCombo('Shift+ArrowLeft', true), '⇧←');
    assert.strictEqual(formatCombo('Meta+ArrowRight', true), '⌘→');
    assert.strictEqual(formatCombo('ArrowDown', true), '↓');
});
test('formatCombo returns canonical string unchanged on non-Mac', () => {
    assert.strictEqual(formatCombo('Ctrl+Shift+Z', false), 'Ctrl+Shift+Z');
    assert.strictEqual(formatCombo('Alt+ArrowUp', false), 'Alt+ArrowUp');
});
test('formatCombo renders ⌫/⌦ delete glyphs on Mac', () => {
    assert.strictEqual(formatCombo('Alt+Backspace', true), '⌥⌫');
    assert.strictEqual(formatCombo('Alt+Shift+Backspace', true), '⌥⇧⌫');
    assert.strictEqual(formatCombo('Alt+Delete', true), '⌥⌦');
});

// ── Integration: the Mac delete-key analogue ────────────────────
test('Mac ⌥+delete event matches the platformized Alt+Backspace combo', () => {
    const combo = platformizeCombo('Alt+Delete', true); // 'Alt+Backspace'
    const macEvent = { altKey: true, code: 'Backspace', key: 'Backspace' };
    assert.strictEqual(matchHotkey(macEvent, combo), true);
});
test('non-Mac Alt+Delete event matches the canonical combo', () => {
    const winEvent = { altKey: true, code: 'Delete', key: 'Delete' };
    assert.strictEqual(matchHotkey(winEvent, 'Alt+Delete'), true);
});

// ── Integration: the Mac ⌘ bug being fixed ──────────────────────
test('Mac ⌘C event matches the platformized Meta+C combo', () => {
    const combo = platformizeCombo('Ctrl+C', true); // 'Meta+C'
    const macEvent = { metaKey: true, ctrlKey: false, code: 'KeyC', key: 'c' };
    assert.strictEqual(matchHotkey(macEvent, combo), true);
    // Documents the bug: the old Ctrl+C combo does NOT match a ⌘ press.
    assert.strictEqual(matchHotkey(macEvent, 'Ctrl+C'), false);
});

test('isTypingTarget', () => {
    assert.strictEqual(isTypingTarget({ tagName: 'TEXTAREA' }), true);
    assert.strictEqual(isTypingTarget({ tagName: 'SELECT' }), true);
    assert.strictEqual(isTypingTarget({ tagName: 'INPUT', type: 'text' }), true);
    assert.strictEqual(isTypingTarget({ tagName: 'INPUT', type: 'checkbox' }), false);
    assert.strictEqual(isTypingTarget({ tagName: 'INPUT', type: 'radio' }), false);
    assert.strictEqual(isTypingTarget({ tagName: 'INPUT', type: 'file' }), false);
    assert.strictEqual(isTypingTarget({ tagName: 'INPUT', type: 'color' }), false);
    assert.strictEqual(isTypingTarget({ tagName: 'INPUT', type: 'button' }), false);
    assert.strictEqual(isTypingTarget({ tagName: 'DIV', isContentEditable: true }), true);
    assert.strictEqual(isTypingTarget(null), false);
});
