// The one way into a logo show: the header mark's hold, a typed word and the console facade all
// arrive here. Desktop twin: the LogoStage hooks in app/MainWindowToolbarHeader.cpp.
import { notify } from '../../utils.js';
import { PRESS_SLOP_PX } from '../popover.js';
import { motionMode } from '../motionPrefs.js';
import { originOf } from '../motion.js';
import { waitForImage } from '../../core/image/imageLoadFlow.js';
import { STAGE, HOLD_MS, TOAST_TEXT, effectOf, resolveShow, heartLine } from './logoStageRules.js';
import { openLogoStage, logoStageAllowed } from './logoStage.js';

// The pink show is an edit, not a stage: a pink page if there is none, the pink tint, and the
// heart as one undoable step.
export const pinkVibe = async (app) => {
  if (!app.image) {
    const previous = app.image;
    await app.createBlankImage({ color: STAGE.pink.blank });
    await waitForImage(app, { previous });
  }
  if (!app.image) return false;
  app.settings.setImageFilter('custom');
  app.settings.setFilterColor(STAGE.pink.tint);
  const { width, height } = app.image;
  const drawn = app.export.installLayout(
    { imageWidth: width, imageHeight: height, lines: [heartLine(width, height)] },
    { mode: 'combine', history: true });
  app.zoomPan?.fitToWindow?.();   // the heart is the show — a page taller than the view hides it
  return drawn;
};

// Every activation: the gate, the show, the toast.
export const activateShow = (name, app, origin = null) => {
  if (!effectOf(name) || !logoStageAllowed()) return false;
  // Every show's notice is the same: the egg on gold, wearing the golden shining.
  if (effectOf(name) === 'pink') {
    const done = pinkVibe(app);
    notify(TOAST_TEXT, 'ok', { shine: true });
    return done;
  }
  if (!openLogoStage(name, { app, origin })) return false;
  notify(TOAST_TEXT, 'ok', { shine: true });
  return true;
};

// The show the header mark's hold opens right now, given the accent and the motion mode.
export const heldShow = (app) => resolveShow(app?.accent, app?.customAccent, motionMode());

// A press held on the mark for holdMs. The release must not reach the logo's own click, which
// would cycle the accent 220ms later.
export const wireLogoHold = (logo, app, { holdMs = HOLD_MS } = {}) => {
  if (!logo || !app) return;
  const wrap = logo.closest?.('.app-logo-wrap') || logo;
  let timer = null, from = null, fired = false;
  const cancel = () => { if (timer) clearTimeout(timer); timer = null; from = null; };
  const swallowClick = (e) => { e.preventDefault?.(); e.stopImmediatePropagation?.(); };
  wrap.addEventListener('pointerdown', (e) => {
    if (e.button || e.altKey || e.ctrlKey || e.metaKey || e.shiftKey) return;
    fired = false;
    from = { x: e.clientX ?? 0, y: e.clientY ?? 0 };
    timer = setTimeout(() => {
      timer = null;
      const name = heldShow(app);
      if (!name) return;
      fired = true;
      wrap.addEventListener('click', swallowClick, { capture: true, once: true });
      activateShow(name, app, originOf(wrap));
    }, holdMs);
  });
  wrap.addEventListener('pointermove', (e) => {
    if (!from) return;
    if (Math.hypot((e.clientX ?? 0) - from.x, (e.clientY ?? 0) - from.y) > PRESS_SLOP_PX) cancel();
  });
  for (const type of ['pointerup', 'pointerleave', 'pointercancel']) {
    wrap.addEventListener(type, () => {
      cancel();
      // A hold that opened a show leaves the swallow armed for exactly one click.
      if (!fired) wrap.removeEventListener('click', swallowClick, { capture: true });
    });
  }
};
