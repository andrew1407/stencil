// ── window.stencil's chainable editor actions ────────────────────────────────
// Transforms, the draw/voice toggles, viewport pan/zoom, and the bulk apply() that
// fans one object out over the settings namespace and these same members.
export const createEditorActions = ({ app }) => {
  let stencil;   // the frozen facade, handed over by setFacade after the guard

  const api = {
    // ── Editor actions (chainable) ──
    rotateLeft() { app.imageModel.rotateImage(-1); return stencil; },
    rotateRight() { app.imageModel.rotateImage(1); return stencil; },
    // Transform the SELECTED line about its bbox centre (same pivot as the per-line rotate) —
    // flip left↔right / top↔bottom, or rotate a quarter turn ±90. No selection is a no-op.
    flipH() { app.flipSelectedLine(true); return stencil; },
    flipV() { app.flipSelectedLine(false); return stencil; },
    rotate90() { app.rotateSelectedLineQuarter(1); return stencil; },
    rotateMinus90() { app.rotateSelectedLineQuarter(-1); return stencil; },
    undo() { app.undo(); return stencil; },
    redo() { app.redo(); return stencil; },
    startDrawing() { app.startDrawingMode(); return stencil; },
    stopDrawing() { app.stopDrawingMode(); return stencil; },
    // Point-adding mode as a get/set toggle (mirrors the Start/Stop drawing buttons).
    // Enabling needs a loaded image (matches the toolbar's guard).
    get drawing() { return !!app.isDrawing; },
    set drawing(on) {
      if (on) { if (app.image && !app.isDrawing) app.startDrawingMode(); }
      else if (app.isDrawing) app.stopDrawingMode();
    },
    // Hands-free voice chat (js/llm/voiceModes.js): listens with the chat closed and sends
    // every utterance as a turn. Turning it on stops any composer dictation.
    get voiceChat() { return !!app.voice?.voiceChat; },
    set voiceChat(on) {
      if (!app.voice) throw new Error('Voice input not ready — the editor UI has not wired yet');
      app.voice.voiceChat = !!on;
    },
    clearLines() { app.clearAllLines(); return stencil; },

    // Pan the canvas viewport by pixel deltas (positive x → right, y → down).
    move({ x = 0, y = 0 } = {}) {
      const vp = document.getElementById('canvas-viewport');
      if (vp) { vp.scrollLeft += Number(x) || 0; vp.scrollTop += Number(y) || 0; }
      return stencil;
    },

    // Zoom by a relative step (0.25 in, -0.4 out). With `point` ({x,y} in image px) the
    // zoom keeps that point fixed on screen; otherwise it recentres.
    zoom(amount, point) {
      const next = app.zoomPan.clampScale((app.scale || 1) + Number(amount || 0));
      if (point && (point.x != null || point.y != null)) app.zoomPan.zoomToImagePoint(next, Number(point.x) || 0, Number(point.y) || 0);
      else app.zoomPan.zoomAroundCenter(next);
      return stencil;
    },
    // Absolute zoom as a percentage (mirrors the toolbar's zoom % input).
    get zoomLevel() { return Math.round((app.scale || 1) * 100); },
    set zoomLevel(pct) { app.zoomPan.zoomAroundCenter(app.zoomPan.clampScale((Number(pct) || 100) / 100)); },
    // Fit the image to the window (the toolbar's "fit" button).
    zoomFit() { app.zoomPan.fitToWindow(); return stencil; },

    // Bulk-apply from one object, then return the facade. Any settings key routes through
    // stencil.settings; plus showTooltip, fullscreen, incognito, zoom, crop, move, layout.
    apply(opts = {}) {
      const set = stencil.settings;
      for (const k of [
        'unit', 'lineColor', 'pointColor', 'pointSize', 'thickness', 'lineStyle',
        'pointStyle', 'showPoints', 'showLines', 'filter', 'filterColor', 'pageSize', 'drawMode',
        'allowFormulas', 'formulaX', 'formulaY', 'fillColor', 'selectionGlow', 'hoverRing', 'focusRing',
      ]) {
        if (opts[k] != null) set[k] = opts[k];
      }
      if (opts.page != null) set.pageSize = opts.page;            // `page` alias for pageSize
      if (opts.showTooltip != null) app.settings.setTooltipOption('enabled', opts.showTooltip);
      if (opts.tooltip && typeof opts.tooltip === 'object')
        for (const k of ['enabled', 'page', 'screen', 'coords'])
          if (opts.tooltip[k] != null) app.settings.setTooltipOption(k, opts.tooltip[k]);
      if (opts.fullscreen != null) stencil.fullscreen = opts.fullscreen;
      if (opts.incognito != null) stencil.incognito = opts.incognito;
      if (opts.zoom != null) stencil.zoom(opts.zoom);
      if (opts.layout != null) stencil.layout = opts.layout;
      if (opts.crop && typeof opts.crop === 'object') stencil.crop(opts.crop);
      if (opts.move && typeof opts.move === 'object') stencil.move(opts.move);
      return stencil;
    },
  };

  return { api, setFacade: (f) => { stencil = f; } };
};
