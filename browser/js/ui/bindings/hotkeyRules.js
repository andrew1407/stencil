import { matchHotkey } from '../../utils.js';
// Chords that must work even while a text box has focus. These windows autofocus an
// input, so a blanket typing guard would make their toggles one-way — able to open the
// panel but never close it from inside the box, exactly when the shortcut is wanted.
export const HOTKEYS_WHILE_TYPING = [
  'toggleChat', 'toggleVoiceChat', 'openHelp', 'openHotkeys', 'openVisuals', 'openProjects', 'openServers', 'openLinks',
  'openDescription', 'openKeywords', 'openAssistantSettings',
];

// Which of those a keydown matches while typing, or null for "let the text box have it".
export const typingHotkeyId = (e, hotkeys, ids = HOTKEYS_WHILE_TYPING) => {
  for (const id of ids) {
    const combo = hotkeys.get(id);
    if (combo && matchHotkey(e, combo)) return id;
  }
  return null;
};

// Where a keyboard-opened context menu (Shift+F10) lands: under the pointer while it
// rests over the canvas, else the viewport's centre — the desktop places its menu the same way.
export const contextMenuPoint = (app, viewportRect) => {
  if (app.mouseOverCanvas) return { x: app.lastMouseClientX, y: app.lastMouseClientY };
  return { x: viewportRect.left + viewportRect.width / 2, y: viewportRect.top + viewportRect.height / 2 };
};
