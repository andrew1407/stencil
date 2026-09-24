// What a stage wears — the mark's art, its cloud's style and whether that cloud flies — resolved
// from the skin, motion mode, accent and theme on <html>, and again on the frame after any moves.
// Browser twin: js/ui/logo/stageLook.js.
import { faviconSvg } from './accents.js';
import { SHOWS, showStyle } from './stageRules.js';
import { stageMark, markHex } from './stagePaint.js';
import { createStageCloud } from './stageCloud.js';

const STYLE_OF = Object.freeze({ particles: 'dust', water: 'water', fire: 'fire' });
const WATCHED = Object.freeze(['data-motion', 'data-accent', 'data-theme', 'data-skin', 'style']);

// The browser's showMotionStyle / showDustAllowed, over the mode in force.
const motionMode = () => globalThis.StencilMotion?.get?.() ?? 'particles';
export const showMotionStyle = () => STYLE_OF[motionMode()] || 'dust';
export const showDustAllowed = () => !!STYLE_OF[motionMode()];

export const createStageLook = (name, app, doc) => {
  let art = null, stale = false, next = null;
  // A fresh <img> has no size until it decodes, so the old mark stays up until the new one can draw.
  const settle = () => { if (next && (next.complete !== false || !look.img)) { look.img = next; next = null; } };
  const look = {
    style: null, dusty: false, glows: true, cloud: null, img: null,
    restyle() {
      stale = false;
      const style = showStyle(name, showMotionStyle());
      const dusty = !!SHOWS[name]?.motion || showDustAllowed();   // a styled show flies its own
      if (!look.cloud || style !== look.style || dusty !== look.dusty) look.cloud = createStageCloud(style);
      look.style = style;
      look.dusty = dusty;
      look.glows = !style || dusty;   // with no cloud flying, only the light-only shows glow
      const hex = markHex(app), svg = faviconSvg(hex);
      if (svg !== art) { art = svg; next = stageMark(doc, hex); }
      settle();
      return look;
    },
    refresh() { if (stale) look.restyle(); else settle(); },
    unwatch() { watch?.disconnect(); },
  };
  const Observer = doc.defaultView?.MutationObserver ?? globalThis.MutationObserver;
  const watch = Observer ? new Observer(() => { stale = true; }) : null;
  watch?.observe(doc.documentElement, { attributes: true, attributeFilter: WATCHED });
  return look.restyle();
};
