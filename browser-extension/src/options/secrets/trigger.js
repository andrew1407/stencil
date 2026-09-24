// The one way into a logo show on this page: the header mark's hold, a typed word and the
// stencil.EasterEggs facade all arrive here. Browser twin: js/ui/logo/stageTrigger.js — the pink
// show paints an image, so it stays the editor's own.
import { SHOW_NAMES, HOLD_MS, TOAST_TEXT, effectOf, resolveShow } from '../../lib/logo/stageRules.js';
import { openLogoStage, logoStageAllowed, closeLogoStage, currentLogoStage } from '../../lib/logo/stage.js';
import { trackPointer } from '../../lib/logo/pointer.js';
import { pageApp } from '../../lib/logo/accents.js';
import { wirePressHold } from '../../lib/logo/hold.js';
import { centerOf } from '../../lib/motion.js';
import { notifyShine } from './toast.js';

// browser config/webcore.json strings.off, pinned by tests/options/secrets.test.js.
export const OFF_TOAST = 'Webcore off — your own look is back';
export const PAGE_SHOWS = Object.freeze(SHOW_NAMES.filter((n) => effectOf(n) !== 'pink'));
const app = pageApp();

const isSkinOn = () => globalThis.document?.documentElement?.getAttribute('data-skin') === 'webcore';
const toggleWebcore = () => {
  const skin = globalThis.StencilSkin;
  return skin ? skin.set(!skin.get()) : false;
};

export const showActive = (name) => {
  const effect = effectOf(name);
  if (effect === 'webcore') return isSkinOn();
  return !!effect && effect !== 'pink' && currentLogoStage()?.name === name;
};

// An asked-for show wins: the stage up, the open confirm dialog answered "no".
export const clearWay = (doc = globalThis.document) => {
  closeLogoStage();
  if (doc?.getElementById?.('confirm-overlay')?.hidden === false) doc.getElementById('confirm-no')?.click?.();
};

// Every activation. The hold is gated to the bare page; `replace` (a call, a typed word) clears
// the way instead, and never restarts the show already up.
export const activateShow = (name, origin = null, { replace = false } = {}) => {
  const effect = effectOf(name);
  if (!effect || effect === 'pink') return false;
  if (effect === 'webcore') {
    if (!replace && !logoStageAllowed()) return false;
    const on = toggleWebcore();
    notifyShine(on ? TOAST_TEXT : OFF_TOAST);
    return on;
  }
  if (replace) {
    if (showActive(name)) return false;
    clearWay();
  }
  if (!openLogoStage(name, { app, origin })) return false;
  notifyShine(TOAST_TEXT);
  return true;
};

export const setShow = (name, on, origin = null) => {
  if (!!on === showActive(name)) return;
  if (on) activateShow(name, origin, { replace: true });
  else if (effectOf(name) === 'webcore') toggleWebcore();
  else closeLogoStage();
};

// The show the header mark's hold opens now, given the accent and the user's own motion mode.
export const heldShow = () =>
  resolveShow(app.accent, app.customAccent,
    globalThis.StencilMotion?.stored?.() ?? globalThis.StencilMotion?.get?.() ?? 'particles');

// A press held on the mark for holdMs opens the show it names, growing out of the mark.
export const wireLogoHold = (wrap, { holdMs = HOLD_MS } = {}) => {
  if (!wrap) return;
  trackPointer(wrap.ownerDocument);
  wirePressHold(wrap, {
    holdMs,
    onHold: () => {
      const name = heldShow();
      if (!name || effectOf(name) === 'pink') return false;
      activateShow(name, centerOf(wrap));
      return true;
    },
  });
};
