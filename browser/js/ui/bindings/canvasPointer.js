import { perFrame } from '../../utils.js';
export function wireCanvasPointer(app) {
  app.canvas.addEventListener('click', e => app.canvasClick(e));
  // Fires only when the click landed on the container itself, so canvas clicks keep flowing
  // through canvasClick() — the letterbox padding around the image counts as empty space.
  const emptyArea = document.getElementById('canvas-viewport');
  if (emptyArea) {
    emptyArea.addEventListener('click', (e) => {
      if (e.target !== e.currentTarget && e.target.id !== 'canvas-container') return;
      app.deselectEmptyArea(e);
    });
    // Hand focus to the canvas, else it stays on the chat textarea and a bare Delete edits chat
    // text instead of the selected line. On pointerdown for pen/touch too; preventScroll.
    emptyArea.addEventListener('pointerdown', () => {
      if (document.activeElement !== app.canvas) app.canvas.focus({ preventScroll: true });
    });
  }
  app.canvas.addEventListener('dblclick', e => app.canvasDblClick(e));
  app.canvas.addEventListener('mousemove', perFrame(e => app.canvasMouseMove(e)), { passive: true });
  app.canvas.addEventListener('mouseleave', (e) => {
    app.mouseOverCanvas = false; app.mouseLeftAt = e.timeStamp;   // outruns a move queued for this frame
    app.tooltipMgr.hide();
    app.updateCoordStatus();
    if (app.hoverPt) { app.hoverPt = null; app.renderer.redraw(); }
    if (app.hoverLineIdx !== -1) { app.hoverLineIdx = -1; app.applyLinesListHover(); }
  });
}
