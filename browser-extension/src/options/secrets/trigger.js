// The one way into a logo show on this page: the header mark's hold, a typed word and the
// stencil.EasterEggs facade all arrive here. Browser twin: js/ui/logo/stageTrigger.js — the pink
// show paints an image, so it stays the editor's own.
import { SHOW_NAMES, HOLD_MS, SKINS, TOAST_TEXT, effectOf } from '../../lib/logo/stageRules.js';
import { openLogoStage, logoStageAllowed, closeLogoStage, currentLogoStage } from '../../lib/logo/stage.js';
import { trackPointer } from '../../lib/logo/pointer.js';
import { pageApp } from '../../lib/logo/accents.js';
import { wirePressHold } from '../../lib/logo/hold.js';
import { centerOf } from '../../lib/motion.js';
import { notifyShine } from './toast.js';
import { AQUA } from '../../lib/aqua/rules.js';
import { skinOn, toggleSkin } from '../../lib/skin/switch.js';
import { heldShow as heldShowOn } from '../../lib/skin/hold.js';

// browser config/webcore.json strings.off, pinned by tests/options/secrets/secrets.test.js.
export const OFF_TOAST = 'Webcore off — your own look is back';
export const AQUA_OFF_TOAST = AQUA.strings.off;
const OFF_TOASTS = Object.freeze({ webcore: OFF_TOAST, aqua: AQUA_OFF_TOAST });
export const PAGE_SHOWS = Object.freeze(SHOW_NAMES.filter((n) => effectOf(n) !== 'pink'));
const app = pageApp();

export const showActive = (name) => {
  const effect = effectOf(name);
  if (SKINS.has(effect)) return skinOn() === effect;
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
  if (SKINS.has(effect)) {
    if (!replace && !logoStageAllowed()) return false;
    const on = toggleSkin(effect);
    notifyShine(on ? TOAST_TEXT : OFF_TOASTS[effect]);
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
  else if (SKINS.has(effectOf(name))) toggleSkin(effectOf(name));
  else closeLogoStage();
};

// The show the header mark's hold opens now (lib/skin/hold.js), on this page.
export const heldShow = () => heldShowOn(globalThis);

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
