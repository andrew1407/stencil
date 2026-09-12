// The single/multi selection set and the two ways to change it (⌘/Ctrl+Shift+click on the
// canvas, a click in the Lines tab).

// Single-select: `selectedLines` is empty and this is [selectedLineIdx]. Always in range.
export const selectedIndices = (app) => {
  const src = app.selectedLines.length ? app.selectedLines : (app.selectedLineIdx >= 0 ? [app.selectedLineIdx] : []);
  return src.filter((i) => i >= 0 && i < app.lines.length);
};

export const isLineSelected = (app, i) => {
  return app.selectedLines.length ? app.selectedLines.includes(i) : i === app.selectedLineIdx;
};

// Ctrl/⌘+Shift+click toggles `idx`; exactly one left drops back to single-select (editor
// reappears), 2+ hides the single-line editor.
export const toggleLineSelection = (app, idx) => {
// Seed the set from the current single selection.
  if (!app.selectedLines.length && app.selectedLineIdx >= 0 && app.selectedLineIdx !== idx)
    app.selectedLines = [app.selectedLineIdx];
  const at = app.selectedLines.indexOf(idx);
  if (at >= 0) app.selectedLines.splice(at, 1);
  else app.selectedLines.push(idx);

  if (app.selectedLines.length === 1) {
    app.selectedLineIdx = app.selectedLines[0];
    app.selectedLines = [];
    app.showSelectionPanel(app.lines[app.selectedLineIdx]);
    app.coordLineIdx = app.selectedLineIdx;
    app.focusedPtIdx = -1;
    app.coordTable.update(app.lines[app.selectedLineIdx].points, app.selectedLineIdx);
  } else {
    app.selectedLineIdx = -1;
    app.hideSelectionPanels();
  }
  app.updateMultiSelectStatus();
  app.renderer.redraw();
};

// "N lines selected" in the status line while multi-selecting (2+). Mirrors the desktop status bar.
export const updateMultiSelectStatus = (app) => {
  const el = document.getElementById('coord-status');
  if (!el) return;
  const n = app.selectedLines.length;
  if (n >= 2) el.textContent = `${n} lines selected — ⌘/Ctrl+Shift+click to add/remove · Alt+Shift+drag to move all · Ctrl+Shift+scroll to rotate all`;
  else if (el.dataset.multi) el.textContent = '';
  el.dataset.multi = n >= 2 ? '1' : '';
  app.renderLinesList();
};

// Select from the Lines tab (or console), keyed by index; clears any multi-selection.
// `ctrlShift` toggles the multi-select set instead.
export const selectLineFromList = (app, idx, ctrlShift = false) => {
  if (idx < 0 || idx >= app.lines.length) return this;
  if (ctrlShift) { toggleLineSelection(app, idx); return this; }
  app.selectedLines = [];
  app.selectedLineIdx = idx;
  app.showSelectionPanel(app.lines[idx]);
  app.coordLineIdx = idx;
  app.focusedPtIdx = -1;
  app.coordTable.update(app.lines[idx].points, idx);
  app.updateMultiSelectStatus();
  app.renderer.redraw();
  return this;
};
