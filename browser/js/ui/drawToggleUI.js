import { icon, DRAW_MODE_ICON } from './icons.js';
import { swapContent, pinWidestFace } from './motion.js';
import { composeControlTitle } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';

// ── The Draw group's two button faces ───────────────────────────
// Start/Stop and Line/Rect. Both are pure view: split out of DrawingApp so core holds no
// icon markup. Driven from updateButtons() (ui/controlState.js), which every isDrawing or
// drawMode change already ends with — one place knows what each button should say.

// The Draw group's single Start/Stop control. Driven from updateButtons(), which every
// isDrawing transition already ends with — one place knows what the button should say.
export const syncDrawToggleUI = (app) => {
  const btn = document.getElementById('draw-toggle');
  if (!btn) return;
  const on = !!app.isDrawing;
  const face = (stop) => icon(stop ? 'stop' : 'play', { size: 13 }) +
    `<span>${stop ? 'Stop' : 'Start'}</span>`;
  // Pin the box to the WIDER of the two faces before swapping into either, so the
  // toggle never resizes under the cursor (measured, not guessed — see motion.js).
  pinWidestFace(btn, [face(false), face(true)]);
  // The face swaps on the shared transition (motion.js): the markup is written
  // synchronously, so however fast the toggling, what the button shows is isDrawing.
  swapContent(btn, face(on), { key: on ? 'stop' : 'start' });
  btn.classList.toggle('active', on);
  // The tooltip's hotkey follows the state too: Alt+A starts, Alt+S stops.
  btn.dataset.hkTitle = on ? 'stopDraw' : 'startDraw';
  btn.dataset.title = on ? 'Stop Drawing' : 'Start Drawing';
  btn.dataset.tip = composeControlTitle(btn, hotkeys.isMac, id => hotkeys.get(id));
};

export const syncDrawModeUI = (app) => {
  const btn = document.getElementById('draw-mode-toggle');
  if (btn) {
    const rect = app.drawMode === 'rect';
    const face = (r) => (r ? DRAW_MODE_ICON.rect : DRAW_MODE_ICON.line) +
      (r ? '<span>Rect</span>' : '<span>Line</span>');
    // Same pin and the same swap as Start/Stop — one idiom for the whole Draw group.
    // The two pairs may settle on different widths; only each button's own stability
    // matters. ("Line" and "Rect" are near-identical, "Start"/"Stop" are not.)
    pinWidestFace(btn, [face(false), face(true)]);
    swapContent(btn, face(rect), { key: app.drawMode });
    btn.dataset.title = app.drawMode === 'rect'
      ? 'Drawing mode: Rectangle (click to switch to Line)'
      : 'Drawing mode: Line (click to switch to Rectangle)';
    btn.dataset.tip = composeControlTitle(btn, hotkeys.isMac, id => hotkeys.get(id));
  }
};
