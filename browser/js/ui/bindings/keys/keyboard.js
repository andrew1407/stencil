import { matchHotkey, isTypingTarget, hasTextSelection } from '../../../utils.js';
import HOTKEY_DEFS from '../../../config/hotkeysConfig.json' with { type: 'json' };
import { hotkeys } from '../../../core/settings/hotkeys.js';
import { hotkeyActions } from './hotkeyActions.js';
import { typingHotkeyId } from './hotkeyRules.js';
export function wireKeyboard(app) {
  const { HK_HANDLERS, EDIT_HOTKEYS, LINE_TRANSFORM_HOTKEYS } = hotkeyActions(app);
  document.addEventListener('keydown', e => {
    if (isTypingTarget(e.target)) {
      const id = typingHotkeyId(e, hotkeys);
      if (id) {
        e.preventDefault();
        HK_HANDLERS[id]?.();
      }
      return;
    }
    // Modifier-free only, so Alt+Delete keeps its own binding and Ctrl/Cmd+Shift+Backspace still
    // deletes the project file.
    if ((e.key === 'Delete' || e.key === 'Backspace') &&
        !e.altKey && !e.ctrlKey && !e.metaKey && !e.shiftKey &&
        !app.isDrawing && !app.compareReadOnly() && app.selectedIndices().length) {
      e.preventDefault();
      app.removeSelectedLines();
      return;
    }
    for (const def of HOTKEY_DEFS) {
      const combo = hotkeys.get(def.id);
      if (!combo) continue;
      if (!matchHotkey(e, combo)) continue;
      // Route the shared chord by selection: yield big-zoom to the flip binding (later in the
      // registry) when a line is selected, fall through when none is.
      if ((def.id === 'zoomInBig' || def.id === 'zoomOutBig') && app.selectedIndices().length >= 1 && !app.compareReadOnly()) continue;
      if (LINE_TRANSFORM_HOTKEYS.has(def.id) && app.selectedIndices().length === 0) continue;
      // Skip 'paste' here — let the browser fire its native paste event
      if (def.id === 'paste') return;
      // Compare view is read-only — swallow editing shortcuts (but keep view/nav ones).
      if (EDIT_HOTKEYS.has(def.id) && app.compareReadOnly()) { e.preventDefault(); return; }
      // With text selected, let the native Ctrl+C variants copy that text instead of hijacking them
      // for copy-image / copy-layout.
      if (['copyImage', 'copyImageOriginal', 'copyImageTint', 'copyLayout'].includes(def.id)
          && hasTextSelection()) return;
      e.preventDefault();
      const fn = HK_HANDLERS[def.id];
      if (fn) fn();
      return;
    }

    // Alt + (=/+/−) ergonomic zoom shortcuts (not in the registry so they can't be remapped away)
    // Shift held → large step (1.0), otherwise small step (0.25)
    if (e.altKey && !e.ctrlKey && !e.metaKey) {
      const inc = e.code === 'Equal' || e.code === 'NumpadAdd';
      const dec = e.code === 'Minus' || e.code === 'NumpadSubtract';
      if (inc || dec) {
        e.preventDefault();
        const step = e.shiftKey ? 1.0 : 0.25;
        app.zoomPan.zoomAroundCenter(app.scale + (inc ? step : -step));
      }
    }
  });

  // Refresh tooltip and cursor the instant a modifier is pressed or released over the canvas, so
  // the Shift and Ctrl tooltips update without re-hovering.
  const onModifierChange = e => {
    if (e.key !== 'Shift' && e.key !== 'Control' && e.key !== 'Alt' && e.key !== 'Meta') return;
    const mods = { ctrlKey: e.ctrlKey, shiftKey: e.shiftKey, altKey: e.altKey, metaKey: e.metaKey };
    app.tooltipMgr.refresh(mods);
    // Live-switch an active segment/line drag the instant Shift is pressed or
    // released, even if the mouse is held still.
    if ((app.isDraggingSegment && app.draggingSegment) ||
        (app.isDraggingLine && app.draggingLine)) {
      app.dragMove(app.lastMouseClientX, app.lastMouseClientY, mods.shiftKey);
    }
    if (app.mouseOverCanvas && !app.isZoomRectDragging && !app.isPanning &&
      !app.isDraggingPoint && !app.isDraggingSegment && !app.isDraggingLine) {
      if (mods.altKey)                         app.canvas.style.cursor = 'grab';
      else if ((mods.ctrlKey || mods.metaKey) && !mods.shiftKey) app.canvas.style.cursor = 'copy';
      else if (mods.shiftKey)                  app.canvas.style.cursor = 'zoom-in';
      else                                     app.canvas.style.cursor = 'crosshair';
    }
  };
  document.addEventListener('keydown', onModifierChange);
  document.addEventListener('keyup', onModifierChange);

  // Physical KeyO (e.code) so it is layout-independent — Mac Option+key produces special e.key
  // chars. Not registry-driven because it needs key-up.
  const setHoldOriginal = on => {
    if (app.compareHoldOriginal === on) return;
    app.compareHoldOriginal = on;
    app.renderer.redraw();
    app.updateButtons();   // read-only peek greys the editing controls too
  };
  document.addEventListener('keydown', e => {
    if (e.repeat || isTypingTarget(e.target)) return;
    if (e.code === 'KeyO' && e.altKey && e.shiftKey && !e.ctrlKey && !e.metaKey && app.image) {
      e.preventDefault();
      setHoldOriginal(true);
    }
  });
  document.addEventListener('keyup', e => {
    // Releasing the letter OR any required modifier ends the peek.
    if (app.compareHoldOriginal &&
        (e.code === 'KeyO' || e.key === 'Alt' || e.key === 'Shift' || e.key === 'Meta' || e.key === 'Control'))
      setHoldOriginal(false);
  });
  // A lost focus (window blur / tab switch) never delivers key-up — drop the peek.
  window.addEventListener('blur', () => setHoldOriginal(false));
}
