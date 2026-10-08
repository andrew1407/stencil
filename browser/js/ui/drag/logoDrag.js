// Dragging the header logo onto the canvas: over it the picture previews its clean view (no
// filter, lines, points or compare split) on the view alone; dropped there, each of those commits
// through the setter its toolbar control uses, so the controls, history and storage follow.
// Desktop twin: desktop/src/app/logo/LogoDrag.{hpp,cpp}.
import { wireIconDrag, markDropTarget } from './iconDrag.js';

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

// `viewport()` is the canvas region; `hold` is the mark's own hold, which a drag drops.
export const logoDragHooks = (app, { viewport, hold = null, menuUp = () => false }) => {
  let over = false;
  const onCanvas = (target) => !!app.image && !!target && !!viewport()?.contains?.(target);
  const preview = (on) => {
    if (over === on) return;
    over = on;
    markDropTarget(viewport(), true, on);
    app.renderer.previewClean(on);
  };
  const end = () => {
    preview(false);
    markDropTarget(viewport(), false);
  };
  return {
    start: ({ event }) => {
      if (refused(event, hold, menuUp)) return false;
      hold?.cancel();
      markDropTarget(viewport(), !!app.image);
      return true;
    },
    move: ({ target }) => preview(onCanvas(target)),
    drop: ({ target }) => {
      if (onCanvas(target)) commitCleanView(app);
      end();
    },
    cancel: end,
  };
};

export const wireLogoDrag = (logo, app, { hold = null } = {}) => {
  if (!logo || !app) return null;
  const viewport = () => app.canvas?.closest?.('.canvas-viewport') ?? null;
  return wireIconDrag(logo, logoDragHooks(app, { viewport, hold, menuUp: () => accentMenuUp(logo) }));
};
