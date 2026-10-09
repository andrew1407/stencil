// Dragging the header logo: over the bare canvas the picture previews its clean view (no filter,
// lines, points or compare split), committed on a drop through each toolbar setter; a line dropped
// on takes the toolbar's style, a control its default (logoTargets.js).
// Desktop twin: desktop/src/app/logo/LogoDrag.{hpp,cpp}.
import { wireIconDrag, markDropTarget } from './iconDrag.js';
import { logoTargetAt, sameTarget, glowOf, dropControlAt, resetDropped } from './logoTargets.js';
import { applyToolbarStyle, setListHoverLine } from '../../core/line/selection.js';

// Only what is applied changes: a picture already unfiltered records no filter step.
export const commitCleanView = (app) => {
  const s = app.settings;
  if ((app.imageFilter ?? 'none') !== 'none') s.setImageFilter('none');
  if (app.showLines) s.setShowLines(false);
  if (app.showPoints) s.setShowPoints(false);
  if ((app.compareMode ?? 'none') !== 'none') s.setCompareMode('none');
};

// A modified press, or any press while the accent menu is up, belongs to the menu; a press whose
// hold opened a show belongs to the show.
const refused = (event, hold, menuUp) =>
  !!(event?.altKey || event?.ctrlKey || event?.metaKey || event?.shiftKey || hold?.fired || menuUp());

const accentMenuUp = (logo) => {
  const menu = logo.closest?.('.app-logo-wrap')?.querySelector?.('.logo-accent-menu');
  return !!menu && !menu.hidden && !menu.classList?.contains('dd-closing');
};

// Over a target: the canvas previews the clean view, a line wears the Lines-tab hover glow, and
// whatever the drop would change glows brighter than the canvas frame offered at the start.
const showTarget = (app, t, on, frame) => {
  if (!t) return;
  if (t.kind === 'clean') app.renderer.previewClean(on);
  if (t.kind === 'line') setListHoverLine(app, on ? t.idx : -1);
  if (t.el === frame) markDropTarget(frame, true, on);
  else markDropTarget(t.kind === 'control' ? glowOf(t.el) : t.el, on, on);
};

// The drop's work: a line takes the toolbar style (its bar re-shows it), a control its default.
const commitTarget = (app, t, reset) => {
  if (t?.kind === 'clean') commitCleanView(app);
  else if (t?.kind === 'control') reset(t.el);
  else if (t?.kind === 'line' && applyToolbarStyle(app, t.idx) && t.idx === app.selectedLineIdx)
    app.showSelectionPanel(app.lines[t.idx]);
};

// `viewport()` is the canvas region; `hold` is the mark's own hold, which a drag drops.
// `controlAt` and `reset` find and reset a control (tests pass their own).
export const logoDragHooks = (app, { viewport, hold = null, menuUp = () => false,
                                     controlAt = dropControlAt, reset = resetDropped }) => {
  let over = null;
  const aim = (next) => {
    if (sameTarget(over, next)) return;
    showTarget(app, over, false, viewport());
    over = next;
    showTarget(app, over, true, viewport());
  };
  const end = () => {
    aim(null);
    markDropTarget(viewport(), false);
  };
  return {
    start: ({ event }) => {
      if (refused(event, hold, menuUp)) return false;
      hold?.cancel();
      markDropTarget(viewport(), !!app.image);
      return true;
    },
    move: (p) => aim(logoTargetAt(app, p, viewport(), controlAt)),
    drop: (p) => {
      const t = logoTargetAt(app, p, viewport(), controlAt);
      end();
      commitTarget(app, t, reset);
    },
    cancel: end,
  };
};

export const wireLogoDrag = (logo, app, { hold = null } = {}) => {
  if (!logo || !app) return null;
  const viewport = () => app.canvas?.closest?.('.canvas-viewport') ?? null;
  return wireIconDrag(logo, logoDragHooks(app, { viewport, hold, menuUp: () => accentMenuUp(logo) }));
};
