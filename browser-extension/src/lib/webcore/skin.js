// The webcore skin's live half, worn by lib/skin/install.js while <html data-skin="webcore"> is
// stamped: every glyph in pixel art, the mark's ring and the hover frame in the preset (--wc-focus:
// the skin points --accent at its own navy). Browser twin: js/ui/webcore/toggle.js.
import { swapIcons } from './icons.js';
import { skinOn } from '../skin/switch.js';
import { MARKS, glyphsAt } from '../skin/page.js';

export const isSkinOn = (doc = globalThis.document) => skinOn(doc) === 'webcore';

const paintFocus = (root, accent) => {
  if (accent) root.style.setProperty('--wc-focus', accent); else root.style.removeProperty('--wc-focus');
};

export const makeWebcoreSkin = (doc) => {
  const root = doc.documentElement;
  return {
    wear: (look) => { paintFocus(root, look.accent); swapIcons(doc.body, true, look); },
    tint: (look) => { paintFocus(root, look.accent); for (const el of glyphsAt(doc.body, MARKS)) swapIcons(el, true, look); },
    shed: () => { root.style.removeProperty('--wc-focus'); swapIcons(doc.body, false); },
    dress: (node, look) => swapIcons(node, true, look),
  };
};
