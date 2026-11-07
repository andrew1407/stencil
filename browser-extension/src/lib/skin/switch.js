// A session skin's switch over the pre-paint window.StencilSkin (lib/prefs/shellPrefs.js): the skin
// <html data-skin> carries, and one skin flipped. Browser twin: js/ui/logo/stageTrigger.js SKIN.
import { SKINS } from '../logo/stageRules.js';

// The skin stamped on the page, or null.
export const skinOn = (doc = globalThis.document) => {
  const name = doc?.documentElement?.getAttribute?.('data-skin');
  return SKINS.has(name) ? name : null;
};

// Each skin's `get`/`set` pair on the facade: webcore is the facade's own, aqua its `aqua` member.
const SWITCHES = Object.freeze({
  webcore: (win) => win.StencilSkin,
  aqua: (win) => win.StencilSkin?.aqua,
});

// Flips `name` and answers the state it is left in; false without the facade.
export const toggleSkin = (name, win = globalThis) => {
  const sw = SWITCHES[name]?.(win);
  return sw ? !!sw.set(!sw.get()) : false;
};
