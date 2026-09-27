import { perFrame } from '../../utils.js';
import { deselectEmptyArea } from '../../core/line/selection.js';
import { canvasClick } from '../../core/pointer/canvasClick.js';
import { canvasMouseMove, canvasDblClick } from '../../core/pointer/hoverController.js';
import { applyLinesListHover } from '../panel/linesList.js';
import { updateCoordStatus } from '../panel/unitDisplay.js';
export function wireCanvasPointer(app) {
  app.canvas.addEventListener('click', e => canvasClick(app, e));
  // Fires only when the click landed on the container itself, so canvas clicks keep flowing
  // through canvasClick() — the letterbox padding around the image counts as empty space.
  const emptyArea = document.getElementById('canvas-viewport');
  if (emptyArea) {
    emptyArea.addEventListener('click', (e) => {
      if (e.target !== e.currentTarget && e.target.id !== 'canvas-container') return;
      deselectEmptyArea(app, e);
    });
    // Hand focus to the canvas, else it stays on the chat textarea and a bare Delete edits chat
    // text instead of the selected line. On pointerdown for pen/touch too; preventScroll.
    emptyArea.addEventListener('pointerdown', () => {
      if (document.activeElement !== app.canvas) app.canvas.focus({ preventScroll: true });
    });
  }
  app.canvas.addEventListener('dblclick', e => canvasDblClick(app, e));
  app.canvas.addEventListener('mousemove', perFrame(e => canvasMouseMove(app, e)), { passive: true });
  app.canvas.addEventListener('mouseleave', (e) => {
    app.mouseOverCanvas = false; app.mouseLeftAt = e.timeStamp;   // outruns a move queued for this frame
    app.tooltip.hide();
    updateCoordStatus(app);
    if (app.hoverPt) { app.hoverPt = null; app.renderer.redraw(); }
    if (app.hoverLineIdx !== -1) { app.hoverLineIdx = -1; applyLinesListHover(app); }
  });
}
