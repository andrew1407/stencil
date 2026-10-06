// ~ pressed twice toggles drawing, beside the Alt+A binding. Physical Backquote (e.code), so it is
// the key left of 1 on any layout; not registry-driven, since a double-press is no combo.
// Desktop twin: app/events/MainWindowEvents.cpp keyPressEvent.
import { isTypingTarget } from '../../../utils.js';
import { toggleDrawing } from '../../../core/draw/mode.js';
import constants from '../../../../../common/config/constants.json' with { type: 'json' };

const { doubleTapMs } = constants.POPOVER;

// True when this press completes a double-press; `now` is ms. A third press starts over.
export const createDoublePress = (windowMs = doubleTapMs) => {
  let last = -Infinity;
  return (now) => {
    if (now - last <= windowMs) { last = -Infinity; return true; }
    last = now;
    return false;
  };
};

export function wireDrawDoublePress(app, doc = document) {
  const press = createDoublePress();
  doc.addEventListener('keydown', (e) => {
    if (e.code !== 'Backquote' || e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
    if (isTypingTarget(e.target) || app.compareReadOnly()) return;
    e.preventDefault();
    if (press(e.timeStamp)) toggleDrawing(app);
  });
}
