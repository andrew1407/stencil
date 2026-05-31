import { test } from 'node:test';
import assert from 'node:assert';
import { normalizeKey, parseHotkey, matchHotkey, comboFromEvent, isTypingTarget } from '../js/utils.js';

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
