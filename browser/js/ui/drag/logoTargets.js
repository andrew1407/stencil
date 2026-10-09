// What the header logo lands on: a line (a Lines-tab row, the selected-line bar, or a line under the
// pointer on the canvas) takes the toolbar's style; a control with a default goes back to it; the
// bare canvas takes the clean view. Desktop twin: desktop/src/app/logo/LogoLineAims.{hpp,cpp}.
import { dropResetTarget, resetControl } from '../control/dblReset.js';
import { canvasCoords } from '../../core/pointer/canvasCoords.js';

const FS_CONTROLS = '#fs-controls-panel';
const LINE_BARS = '#selection-panel, #fs-selection-panel';

// The fullscreen panel's controls are clones keeping their ids; a reset lands on the original.
export const originalOf = (el) => {
  if (!el?.id || !el.closest?.(FS_CONTROLS)) return el;
  return el.ownerDocument?.querySelector?.(`#controls-body #${el.id}`) ?? el;
};

/** Resets `el` (a clone resolves to its original) and keeps the clone showing what it holds. */
export const resetDropped = (el) => {
  const orig = originalOf(el);
  const moved = resetControl(orig);
  if (moved && orig !== el) {
    el.value = orig.value;
    if (orig.type === 'checkbox') el.checked = orig.checked;
  }
  return moved;
};

/** The control a drop on `target` resets, when it is enabled. */
export const dropControlAt = (target) => {
  const el = dropResetTarget(target);
  return el && !el.disabled ? el : null;
};

// What glows for a control: its custom select's face, its toggle pill or label, or itself.
export const glowOf = (el) => el.closest?.('.cs-dd, .pill-toggle, label') ?? el;

const lineIndex = (app, i) => (Number.isInteger(i) && i >= 0 && i < (app.lines?.length ?? 0) ? i : -1);

// A line on the canvas is found where the pointer is, through the app's own hit test.
const canvasLine = (app, x, y) => {
  if (!app.lines?.length || typeof app.findLineAt !== 'function' || !app.canvas) return -1;
  const at = canvasCoords(app, x, y);
  return lineIndex(app, app.findLineAt(at.x, at.y));
};

/** `{ kind: 'line', idx, el } | { kind: 'control', el } | { kind: 'clean', el } | null` for a
 *  pointer at (x, y) over `target`; `viewport` is the canvas region. */
export const logoTargetAt = (app, { target, x, y }, viewport, controlAt = dropControlAt) => {
  if (!target) return null;
  const row = target.closest?.('tr.lines-row');
  if (row) {
    const idx = lineIndex(app, parseInt(row.dataset?.idx, 10));
    return idx >= 0 ? { kind: 'line', idx, el: row } : null;
  }
  const bar = target.closest?.(LINE_BARS);
  if (bar) {
    const idx = lineIndex(app, app.selectedLineIdx);
    return idx >= 0 ? { kind: 'line', idx, el: bar } : null;
  }
  const control = controlAt(target);
  if (control) return { kind: 'control', el: control };
  if (!app.image || !viewport || !(target === viewport || viewport.contains?.(target))) return null;
  const idx = canvasLine(app, x, y);
  return idx >= 0 ? { kind: 'line', idx, el: viewport } : { kind: 'clean', el: viewport };
};

export const sameTarget = (a, b) => (a === b) || (!!a && !!b && a.kind === b.kind && a.el === b.el && a.idx === b.idx);
