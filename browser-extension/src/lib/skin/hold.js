// The header mark's hold: the show it resolves from the accent, the user's own motion mode and the
// face (logoStage.json), and the press-hold that flips the skin it names. Browser twin:
// js/ui/logo/stageTrigger.js heldShow / wireLogoHold.
import { HOLD_MS, SKINS, effectOf, resolveShow } from '../logo/stageRules.js';
import { wirePressHold } from '../logo/hold.js';
import { pageFace } from '../logo/accents.js';
import { toggleSkin } from './switch.js';

// The stored accent, or under aqua its session Blue on screen (so the hold turns the skin off); the
// user's own motion mode, a skin's hold set aside. null without the pre-paint facades.
export const heldShow = (win = globalThis) => {
  const accent = win.StencilAccent, motion = win.StencilMotion;
  if (!accent || !motion || !win.StencilSkin) return null;
  const root = win.document?.documentElement;
  const custom = root?.style?.getPropertyValue('--accent') || null;
  const shown = win.StencilSkin.aqua?.get?.() ? root?.getAttribute?.('data-accent') : null;
  return resolveShow(shown || accent.get(), custom, motion.stored?.() ?? motion.get(), pageFace(root ?? null));
};

// The skin the hold would flip ('webcore' | 'aqua'), or null.
export const heldSkin = (win = globalThis) => {
  const effect = effectOf(heldShow(win));
  return SKINS.has(effect) ? effect : null;
};

export const wireSkinHold = (wrap, { holdMs = HOLD_MS, win = globalThis } = {}) =>
  wirePressHold(wrap, {
    holdMs,
    onHold: () => {
      const skin = heldSkin(win);
      if (skin) toggleSkin(skin, win);
      return !!skin;
    },
  });
