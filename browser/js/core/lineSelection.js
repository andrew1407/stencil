// ── Which lines are selected ────────────────────────────────────
// Split out of DrawingApp: the single/multi selection set and the two ways to change it
// (⌘/Ctrl+Shift+click on the canvas, a click in the Lines tab). Reached through the
// same-named methods on the app.

// The set of currently-selected line indices. In ordinary single-select mode `selectedLines`
// is empty and this returns [selectedLineIdx] (or []) — so nothing else changes; in multi-select
// mode it returns the explicit set. Always filtered to valid, in-range indices.
export const selectedIndices = (app) => {
  const src = app.selectedLines.length ? app.selectedLines : (app.selectedLineIdx >= 0 ? [app.selectedLineIdx] : []);
  return src.filter((i) => i >= 0 && i < app.lines.length);
};

// True while `i` is part of the current selection (single or multi) — drives the renderer glow.
export const isLineSelected = (app, i) => {
  return app.selectedLines.length ? app.selectedLines.includes(i) : i === app.selectedLineIdx;
};

// Ctrl/⌘+Shift+click: add/remove `idx` from the multi-select set. Clicking a line already in the
// set removes it. With exactly one line left, we drop back to normal single-select (its editor
// reappears); with 2+, the single-line editor is hidden (ambiguous which line to edit).
export const toggleLineSelection = (app, idx) => {
  // Seed the set from the current single selection the first time you Ctrl+Shift+click.
  if (!app.selectedLines.length && app.selectedLineIdx >= 0 && app.selectedLineIdx !== idx)
    app.selectedLines = [app.selectedLineIdx];
  const at = app.selectedLines.indexOf(idx);
  if (at >= 0) app.selectedLines.splice(at, 1);
  else app.selectedLines.push(idx);

  if (app.selectedLines.length === 1) {
    // Back to a single selection — restore its editor + coord table.
    app.selectedLineIdx = app.selectedLines[0];
    app.selectedLines = [];
    app.showSelectionPanel(app.lines[app.selectedLineIdx]);
    app.coordLineIdx = app.selectedLineIdx;
    app.focusedPtIdx = -1;
    app.coordTable.update(app.lines[app.selectedLineIdx].points, app.selectedLineIdx);
  } else {
    // 0 or 2+ selected: no single-line editor.
    app.selectedLineIdx = -1;
    app.hideSelectionPanels();
  }
  app.updateMultiSelectStatus();
  app.renderer.redraw();
};

// Show a brief "N lines selected" note in the status line while multi-selecting (2+); clear it
// otherwise. Mirrors the desktop status bar.
export const updateMultiSelectStatus = (app) => {
  const el = document.getElementById('coord-status');
  if (!el) return;
  const n = app.selectedLines.length;
  if (n >= 2) el.textContent = `${n} lines selected — ⌘/Ctrl+Shift+click to add/remove · Alt+Shift+drag to move all · Ctrl+Shift+scroll to rotate all`;
  else if (el.dataset.multi) el.textContent = '';
  el.dataset.multi = n >= 2 ? '1' : '';
  app.renderLinesList();
};

// Select a single line from the "Lines" tab list (or console) — mirrors the canvas
// "click on a segment" path (canvasClick priority 2), but keyed by index so the list
// and the canvas stay in sync. Clears any multi-selection first. `ctrlShift` toggles it
// into/out of the multi-select set instead (so the list mirrors ⌘/Ctrl+Shift+click).
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
