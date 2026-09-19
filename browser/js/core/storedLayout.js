// A stored layout (a projectsStore payload) back into editor state, in the groups the
// window-side adapter paints between: page, drawing, provenance, formulas, tools.

export const applyStoredPage = (app, layout) => {
  if (layout.customPageWidth) app.customPageWidth = layout.customPageWidth;
  if (layout.customPageHeight) app.customPageHeight = layout.customPageHeight;
  // Stored values are cm; applyUnitToUI converts for display.
  if (layout.unit) app.unit = layout.unit;
  app.applyUnitToUI();
};

export const applyStoredDrawing = (app, layout) => {
  if (layout.color) app.color = layout.color;
  // '' is MEANINGFUL ("points follow the line colour"), hence the typeof check.
  if (typeof layout.pointColor === 'string') app.pointColor = layout.pointColor;
  if (layout.thickness) app.thickness = layout.thickness;
  if (layout.pointSize) app.pointSize = layout.pointSize;
  if (layout.style) app.style = layout.style;
  app.showPoints = layout.showPoints !== undefined ? layout.showPoints : true;
  app.showLines = layout.showLines !== undefined ? layout.showLines : true;
  app.imageFilter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
  if (layout.filterColor) app.filterColor = layout.filterColor;
};

export const applyStoredProvenance = (app, layout) => {
  app.imageBaseName = layout.imageBaseName || null;
  app.imageExt = layout.imageExt || null;
  app.imageSource = layout.imageSource || null;
  app.imageResource = layout.imageResource || null;
  if (layout.tooltipEnabled !== undefined) app.tooltipEnabled = layout.tooltipEnabled;
  if (layout.tooltipShowPage !== undefined) app.tooltipShowPage = layout.tooltipShowPage;
  if (layout.tooltipShowScreen !== undefined) app.tooltipShowScreen = layout.tooltipShowScreen;
  if (layout.tooltipShowCoords !== undefined) app.tooltipShowCoords = layout.tooltipShowCoords;
};

// The shared sync (settingsController.js) keeps the toolbar pill's .on class in step.
export const applyStoredFormulas = (app, layout) => {
  app.allowFormulas = layout.allowFormulas !== undefined ? layout.allowFormulas : false;
  app.settings.syncFormulaUI(app.allowFormulas);
  app.formulaX = layout.formulaX || '';
  app.formulaY = layout.formulaY || '';
};

export const applyStoredTools = (app, layout) => {
  if (layout.drawMode) app.drawMode = layout.drawMode;
  if (Number.isFinite(layout.holdDrawDelay))
    app.input.setHoldDrawDelay(layout.holdDrawDelay, { persist: false });
  if (layout.selGlowColor) app.selGlowColor = layout.selGlowColor;
  if (layout.hoverRingColor) app.hoverRingColor = layout.hoverRingColor;
  if (layout.focusRingColor) app.focusRingColor = layout.focusRingColor;
  if (layout.defaultFillColor) app.defaultFillColor = layout.defaultFillColor;
  app.syncDrawModeUI();
};

const clearImage = (app) => {
  app.image = null;
  app.originalImage = null;
  app.cropRect = null;
  app.rotationQuarters = 0;
  app.imageDataUrl = null;
  app.lines = [];
  app.history.reset([], -1);
};

const repaintEmpty = (app) => {
  app.updateInfo();
  app.renderer.redraw();
  app.updateButtons();
};

// No image in the payload: either it was too large for storage, so the lines wait for a
// re-upload, or the payload carried nothing but settings.
export const applyImagelessPayload = (storage, layout) => {
  const app = storage.app;
  clearImage(app);
  if ((layout.lines || []).length > 0) {
    app.pendingLines = layout.lines;
    app.pendingImageSize = { w: layout.imageWidth, h: layout.imageHeight };
    repaintEmpty(app);
    storage.showImageMissingBanner(true);
    app.showSaveStatus('Re-upload image to restore drawing', 'var(--warning)', 'alert');
    return;
  }
  repaintEmpty(app);
  app.showSaveStatus('Settings restored', 'var(--accent)', 'refresh');
};
