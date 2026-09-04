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

// Alt+G opened the assistant but could never close it: opening focuses the chat input, and
// the keydown handler drops every chord that lands on a text box. A short allow-list gets the
// toggle back without letting canvas shortcuts fire mid-word.
test('typingHotkeyId lets the chat toggle through a focused text box, nothing else', async () => {
  const { typingHotkeyId } = await import('../js/core/controlsBinder.js');
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

// The window shortcuts must survive a focused search box: every one of these panels
// autofocuses its filter field, so without this they could be opened by shortcut and never
// closed by it (the Alt+G report, and the same for help/projects/servers).
test('the window shortcuts, not the editing ones, work from inside a text box', async () => {
  const { typingHotkeyId, HOTKEYS_WHILE_TYPING } = await import('../js/core/controlsBinder.js');
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openHelp'));
  assert.ok(HOTKEYS_WHILE_TYPING.includes('openProjects'));
  const hotkeys = new Map([['openHelp', 'Alt+H'], ['cycleFilter', 'Alt+B']]);
  const press = (key, mods = {}) => ({ key, code: `Key${key.toUpperCase()}`, altKey: false,
    ctrlKey: false, metaKey: false, shiftKey: false, ...mods });
  assert.equal(typingHotkeyId(press('h', { altKey: true }), hotkeys), 'openHelp');
  assert.equal(typingHotkeyId(press('b', { altKey: true }), hotkeys), null);   // stays typing
});
