import { icon, DRAW_MODE_ICON } from './icons.js';
import { swapContent, pinWidestFace } from './motion.js';
import { composeControlTitle } from '../utils.js';
import { hotkeys } from '../core/settings/hotkeys.js';

// The Draw group's Start/Stop and Line/Rect faces, driven from updateButtons()
// (ui/controlState.js) so core holds no icon markup.
export const syncDrawToggleUI = (app) => {
  const btn = document.getElementById('draw-toggle');
  if (!btn) return;
  const on = !!app.isDrawing;
  const face = (stop) => icon(stop ? 'stop' : 'play', { size: 13 }) +
    `<span>${stop ? 'Stop' : 'Start'}</span>`;
// Pin the box to the wider face before swapping, so the toggle never resizes under the cursor.
  pinWidestFace(btn, [face(false), face(true)]);
  swapContent(btn, face(on), { key: on ? 'stop' : 'start' });
  btn.classList.toggle('active', on);
// Alt+A starts, Alt+S stops.
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
    pinWidestFace(btn, [face(false), face(true)]);
    swapContent(btn, face(rect), { key: app.drawMode });
    btn.dataset.title = app.drawMode === 'rect'
      ? 'Drawing mode: Rectangle (click to switch to Line)'
      : 'Drawing mode: Line (click to switch to Rectangle)';
    btn.dataset.tip = composeControlTitle(btn, hotkeys.isMac, id => hotkeys.get(id));
  }
};
