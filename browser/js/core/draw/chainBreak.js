import { startDrawingMode } from './mode.js';
import { CHANGE, changed } from '../app/changes.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

// Breaking the chain while drawing (⌘/Ctrl+click, double-click, double-tap): the stroke so far
// is kept — a lone point too — and an unconnected stroke opens at the press, drawing stays on.
// Desktop twin: canvas/draw/CanvasChainBreak.cpp.

// The point the last drawing click placed, held by OBJECT: a later insert shifts indices.
const lastDrop = new WeakMap();
export const noteDrop = (app, line, pt) => { lastDrop.set(app, { line, pt }); };

// A dropped point's segment waits out the double-click window, so a double-click never shows one.
const { doubleClickMs, doubleTapMs } = constants.POPOVER;
export const dropHoldMs = (e) => (e?.pointerType === 'touch' ? doubleTapMs : doubleClickMs);

export const breakChainAt = (app, x, y) => {
  app.strokeFx.cancel?.();
  let kept = -1;
  if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
    kept = app.continueLineIdx;
  } else if (app.currentLine && app.currentLine.points.length > 0) {
    app.lines.push(app.currentLine);
    kept = app.lines.length - 1;
  }
  app.continueLineIdx = -1;
  app.continueInsertIdx = -1;
  app.currentLine = null;
  if (kept >= 0) {
    app.coordLineIdx = kept;
    app.coordTable.update(app.lines[kept].points, kept);
    app.saveHistory();
  }
  startDrawingMode(app, { connect: false });
  if (!app.currentLine) return;
  const pt = { x, y };
  app.currentLine.points.push(pt);
  app.strokeFx.flyIn(app.currentLine, 0);
  noteDrop(app, app.currentLine, pt);
  app.renderer.redraw();
  changed(app, CHANGE.lines, CHANGE.history);
};

// The second click of a double-click lands where the first one just dropped a point: take
// that point back off its chain and open the new chain there instead. False if it did not.
export const breakChainOnRepeat = (app, x, y) => {
  const drop = lastDrop.get(app);
  lastDrop.delete(app);
  if (!drop) return false;
  const i = drop.line.points.indexOf(drop.pt);
  const slop = constants.HIT.grabRadiusPx / (app.scale || 1);
  if (i < 0 || Math.hypot(drop.pt.x - x, drop.pt.y - y) > slop) return false;
  drop.line.points.splice(i, 1);
  if (drop.line === app.lines[app.continueLineIdx] && i < app.continueInsertIdx) app.continueInsertIdx--;
  breakChainAt(app, drop.pt.x, drop.pt.y);
  return true;
};
