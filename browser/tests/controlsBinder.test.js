import { test } from 'node:test';
import assert from 'node:assert';

// The helper moved to utils.js when the projects modal's per-row swatch needed it too —
// ui/ must not import the (heavy, ui-importing) controls binder.
test('anchorPickerInput pins the hidden colour input under the invoking button', async () => {
  const { anchorPickerInput } = await import('../js/utils.js');
  const input = { style: {} };
  const btn = { getBoundingClientRect: () => ({ left: 411.6, bottom: 92.2 }) };
  anchorPickerInput(input, btn);
  assert.equal(input.style.position, 'fixed');
  assert.equal(input.style.left, '412px');
  assert.equal(input.style.top, '92px');
  // No button (or no rect) → leave the input untouched rather than throwing.
  const untouched = { style: {} };
  anchorPickerInput(untouched, null);
  assert.deepEqual(untouched.style, {});
});

// The keydown handler drops every chord that lands on a text box and opening the assistant focuses its
// input, so a short allow-list keeps the toggle working without firing canvas shortcuts mid-word.
test('typingHotkeyId lets the chat toggle through a focused text box, nothing else', async () => {
  const { typingHotkeyId } = await import('../js/ui/bindings/hotkeyRules.js');
  const hotkeys = new Map([['toggleChat', 'Alt+G'], ['undo', 'Ctrl+Z'], ['deleteLine', 'Alt+Delete']]);
  const press = (key, mods = {}) => ({ key, code: `Key${key.toUpperCase()}`, altKey: false,
    ctrlKey: false, metaKey: false, shiftKey: false, ...mods });

  assert.equal(typingHotkeyId(press('g', { altKey: true }), hotkeys), 'toggleChat');
  // Everything else stays the text box's: typing must not undo the drawing or delete a line.
  assert.equal(typingHotkeyId(press('z', { ctrlKey: true }), hotkeys), null);
  assert.equal(typingHotkeyId({ key: 'Delete', code: 'Delete', altKey: true, ctrlKey: false,
    metaKey: false, shiftKey: false }, hotkeys), null);
  assert.equal(typingHotkeyId(press('g'), hotkeys), null);
  // An unbound toggle claims nothing.
  assert.equal(typingHotkeyId(press('g', { altKey: true }), new Map()), null);
});

// Every one of these panels autofocuses its filter field, so the window shortcuts must survive a focused
// search box, or a panel opens by shortcut and can never be closed by it.
test('the window shortcuts, not the editing ones, work from inside a text box', async () => {
  const { typingHotkeyId, HOTKEYS_WHILE_TYPING } = await import('../js/ui/bindings/hotkeyRules.js');
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openHelp'));
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openProjects'));
  // Voice chat must be switchable from inside the composer too (its own dictation lands there).
  assert.ok(HOTKEYS_WHILE_TYPING.includes('toggleVoiceChat'));
  // The description / keywords windows autofocus their textarea, so their chords must
  // close them from inside it too (the same one-way trap as the chat toggle).
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openLinks'));
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openDescription'));
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openKeywords'));
  // The assistant settings window autofocuses its fields too, so its chord must close it
  // from inside them — and it lives in the shared registry so the desktop lists it as well.
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openAssistantSettings'));
  const hotkeys = new Map([['openHelp', 'Alt+H'], ['cycleFilter', 'Alt+B']]);
  const press = (key, mods = {}) => ({ key, code: `Key${key.toUpperCase()}`, altKey: false,
    ctrlKey: false, metaKey: false, shiftKey: false, ...mods });
  assert.equal(typingHotkeyId(press('h', { altKey: true }), hotkeys), 'openHelp');
  assert.equal(typingHotkeyId(press('b', { altKey: true }), hotkeys), null);   // stays typing
});

// Shift+F10 opens the canvas context menu from the keyboard (the desktop binds the same chord): under the
// pointer while it rests on the canvas, else at the viewport's centre, always through a real event.
test('contextMenu hotkey: Shift+F10 in the registry, placed at the pointer or the viewport centre', async () => {
  const { readFileSync } = await import('node:fs');
  const defs = JSON.parse(readFileSync(new URL('../js/config/hotkeysConfig.json', import.meta.url), 'utf8'));
  const def = defs.find(d => d.id === 'contextMenu');
  assert.ok(def, 'contextMenu is a rebindable registry entry');
  assert.equal(def.default, 'Shift+F10');
  const { contextMenuPoint } = await import('../js/ui/bindings/hotkeyRules.js');
  const vp = { left: 100, top: 50, width: 600, height: 400 };
  assert.deepEqual(contextMenuPoint({ mouseOverCanvas: true, lastMouseClientX: 320, lastMouseClientY: 240 }, vp),
    { x: 320, y: 240 });
  assert.deepEqual(contextMenuPoint({ mouseOverCanvas: false, lastMouseClientX: 320, lastMouseClientY: 240 }, vp),
    { x: 400, y: 250 });
  // The handler dispatches a contextmenu MouseEvent on the viewport and never opens a second menu.
  const src = readFileSync(new URL('../js/ui/bindings/hotkeyActions.js', import.meta.url), 'utf8');
  const body = src.slice(src.indexOf('contextMenu: () => {'), src.indexOf('};', src.indexOf('contextMenu: () => {')));
  assert.ok(body.includes("new MouseEvent('contextmenu'"), 'goes through the right-click path');
  assert.ok(body.includes("classList.contains('ctx-open')"), 'an open menu is left alone');
});

// The AI settings window (the chat's … menu ▸ Settings, both apps) has a rebindable chord
// of its own, next to the assistant toggle's Alt+G, and it collides with nothing else.
test('openAssistantSettings is a registry entry on Alt+Shift+G with a unique default', async () => {
  const { readFileSync } = await import('node:fs');
  const defs = JSON.parse(readFileSync(new URL('../js/config/hotkeysConfig.json', import.meta.url), 'utf8'));
  const def = defs.find(d => d.id === 'openAssistantSettings');
  assert.ok(def, 'openAssistantSettings is a rebindable registry entry');
  assert.equal(def.default, 'Alt+Shift+G');
  assert.equal(def.label, 'AI Assistant Settings');
  assert.equal(defs.filter(d => d.default === def.default).length, 1, 'the default chord is unique');
});
