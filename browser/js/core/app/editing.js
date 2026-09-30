// DrawingApp's own editing behaviour that needs no DOM: the hit-test defaults, selection and
// drag starts, the undo history, and the small session answers. Installed onto the app's
// prototype as they are (delegates.js installMethods); `this` is the app.
import { notify, compareEditedShows } from '../../utils.js';
import * as hitTest from '../draw/hitTest.js';
import * as dragGestures from '../touch/dragGestures.js';
import * as launch from '../launch/controller.js';
import { editorMemento, sameFilter } from '../historyStack.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { updateMultiSelectStatus } from '../line/selection.js';
import { CHANGE, changed } from './changes.js';

const { HIT } = constants;

export class EditingMethods {
// Desktop needs a configured URL scheme; Telegram needs a bot username AND a server
// project (a 64-char t.me start payload can't carry image bytes).
  openInDesktopAvailable() { return !!this.openInConfig?.desktopScheme; }
  openInTelegramAvailable() { return !!this.openInConfig?.telegramBotUsername && !!this.remoteLink; }
  openInAvailable() { return this.openInDesktopAvailable() || this.openInTelegramAvailable(); }

// A comparison view (original / split, or the Alt+Shift+O peek) is read-only: hover,
// selection and every editing gesture are suppressed; only pan/zoom and the divider drag stay live.
  compareReadOnly() {
    return this.renderer.effectiveCompareMode() !== 'none';
  }

// The tooltip is DISPLAY, so it stays live in a comparison for points you can SEE — gated
// on the POINT's coordinates, never the cursor's.
  compareShowsPoint(x, y) {
    return compareEditedShows(this.renderer.effectiveCompareMode(), this.compareSplit,
      x, y, this.canvas.width, this.canvas.height);
  }

  importExternalImage(payload, { mode = 'new' } = {}) { return launch.importExternalImage(this, payload, { mode }); }

// `opts.from` is the drop point of a drag-and-drop.
  loadJSONFromFile(file, opts = {}) {
    const reader = new FileReader();
    reader.onload = event => {
      try {
        const data = JSON.parse(event.target.result);
        if (!data || !Array.isArray(data.lines)) {
          notify('File is not a valid layout', 'fail');
          return;
        }
        this.export.applyPastedLayout(data, opts.from);
      } catch (err) {
        notify('Error loading JSON: ' + err.message, 'fail');
      }
    };
    reader.readAsText(file);
  }

// Default thresholds are a screen-px radius divided by the zoom, so hits stay constant on screen.
  findLineAt(x, y, threshold = HIT.lineRadiusPx / (this.scale || 1)) {
    return hitTest.findLineAt(this.lines, x, y, threshold);
  }

  deselectLine(redraw = true) {
    this.selectedLineIdx = -1;
    this.selectedLines = [];
    updateMultiSelectStatus(this);
    this.coordLineIdx = -1;
    this.hoveredPtIdx = -1;
    this.focusedPtIdx = -1;
    this.hideSelectionPanels();
    if (redraw) this.renderer.redraw();
  }

  findNearestPoint(x, y, threshold = HIT.pointRadiusPx / (this.scale || 1)) {
    return hitTest.findNearestPoint(this.lines, this.currentLine, x, y, threshold);
  }

// Alt+Ctrl/⌘+drag: pull a NEW point out of the line under the cursor; a closed area breaks
// open at that spot (dragGestures.pullOutPoint). Returns whether a drag began.
  beginPullOutDrag(x, y) {
    if (this.compareReadOnly()) return false;
    const nearPt = this.findNearestPointWithIdx(x, y);
    const target = (nearPt && nearPt.lineIdx !== -1)
      ? { kind: 'point', ptIdx: nearPt.ptIdx, lineIdx: nearPt.lineIdx }
      : this.findNearestSegmentWithIdx(x, y);
    if (!target || target.lineIdx === undefined || target.lineIdx < 0) return false;
    const line = this.lines[target.lineIdx];
    if (!line) return false;
    const wasArea = !!line.locked;
    const idx = dragGestures.pullOutPoint(line, target.kind ? target : { ...target, kind: 'segment' }, x, y);
    if (idx < 0) return false;
    this.strokeFx.flyIn(line, idx, { x, y });
    this.selectedLineIdx = target.lineIdx;
    this.coordLineIdx = target.lineIdx;
    this.focusedPtIdx = idx;
    this.isDraggingPoint = true;
    this.draggingPoint = { lineIdx: target.lineIdx, ptIdx: idx };
    this.showSelectionPanel(line);
    this.coordTable.update(line.points, target.lineIdx);
    this.renderer.redraw();
    changed(this, CHANGE.lines, CHANGE.selection);
    if (wasArea) notify('Area unchained — drag the loose end', 'ok');
    return true;
  }

// The ✂ in the selection panel.
  unchainSelectedLine() {
    if (this.compareReadOnly()) return;
    const line = this.lines[this.selectedLineIdx];
    if (!dragGestures.unchainLine(line)) { notify('Selected line is not an area', 'info'); return; }
    this.focusedPtIdx = -1;
    this.showSelectionPanel(line);
    this.coordTable.update(line.points, this.selectedLineIdx);
    this.saveHistory();
    this.renderer.redraw();
    changed(this, CHANGE.lines);
    notify('Area unchained — it is an open line again', 'ok');
  }

  findNearestPointWithIdx(x, y, threshold = HIT.grabRadiusPx / (this.scale || 1)) {
    return hitTest.findNearestPointWithIdx(this.lines, this.currentLine, x, y, threshold);
  }

  findNearestSegmentWithIdx(x, y, threshold = HIT.grabRadiusPx / (this.scale || 1)) {
    return hitTest.findNearestSegmentWithIdx(this.lines, x, y, threshold);
  }

  saveHistory() {
    this.history.push(editorMemento(this));
    this.storage.saveSoon();    // trailing-edge: a stroke persists once, when it stops
    this.remoteSync.scheduleRemoteSync();
    changed(this, CHANGE.history);
  }

  undo() {
    if (this.compareReadOnly()) return;
    if (this.isDrawing && this.currentLine) {
      if (this.currentLine.points.length > 0) {
        this.undonePoints.push(this.currentLine.points.pop());
        this.renderer.redraw();
        changed(this, CHANGE.history);
      }
      return;
    }
    const result = this.history.undo();
    if (result !== null) {
      this.restoreHistoryStep(result);
      this.renderer.redraw();
      changed(this, CHANGE.history, CHANGE.lines);
      this.coordTable.update();
    }
  }

  redo() {
    if (this.compareReadOnly()) return;
    if (this.isDrawing && this.currentLine) {
      if (this.undonePoints?.length > 0) {
        this.currentLine.points.push(this.undonePoints.pop());
        this.renderer.redraw();
        changed(this, CHANGE.history);
      }
      return;
    }
    const result = this.history.redo();
    if (result !== null) {
      this.restoreHistoryStep(result);
      this.renderer.redraw();
      changed(this, CHANGE.history, CHANGE.lines);
      if (this.lines.length > 0) this.coordTable.update(this.lines[this.lines.length - 1].points);
    }
  }

// A step's lines, and — for a memento — the crop, turn and filter it was taken on: the view
// re-derived from the original without a decode, the filter set as the toolbar sets it.
  restoreHistoryStep(step) {
    const m = Array.isArray(step) ? { lines: step } : step;
    this.strokeFx.cancel();     // the snapshot's points are not the ones in the air
    this.lines = m.lines;
    this.hoverPt = null;        // the restored snapshot may not contain the hovered indices
    this.hoverLineIdx = -1;
    this.listHoverLineIdx = -1;
    if (m.filter && !sameFilter(m, this))
      this.settings.setFilter({ filter: m.filter, filterColor: m.filterColor || undefined }, { history: false });
    if (this.imageModel.restoreView(m)) this.imageModel.settleView();
  }

// `color` is a status token every call site picks; it selects the toast kind.
  showSaveStatus(msg, color) {
    const kind = String(color).includes('danger') ? 'fail'
               : String(color).includes('success') ? 'ok' : 'info';
// `key` makes the newer message replace the older toast (a blank-image create saves twice).
    notify(msg, kind, { key: 'save-status' });
  }

// Drives the beforeunload leave-guard in index.js. Kept synchronous: beforeunload can't
// await the confirm() modal, so the browser's native prompt is used.
  hasEditingSession() {
    return !!this.image || (typeof this.history?.canUndo === 'function' && this.history.canUndo());
  }

  closeProject(id, opts = {}) {
    this.projectTransfer.closeProject(id, opts);
    return this;
  }
}
