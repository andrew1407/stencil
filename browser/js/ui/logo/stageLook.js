// What a stage wears — the mark's art, its cloud's style and whether that cloud flies — resolved
// from the live skin, motion mode, accent and theme, and again on the frame after any of them moves.
// Desktop twin: app/LogoStage.cpp.
import { showDustAllowed, showMotionStyle } from '../motion/motionPrefs.js';
import { faviconSvg } from '../../core/settings/accents.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { SHOWS, showStyle } from './stageRules.js';
import { stageMark, markHex } from './stagePaint.js';
import { createStageCloud } from './stageCloud.js';

// A frame late on purpose: the webcore toggle announces its motion before it swaps the mark's art.
const WATCHED = [EVENTS.motionChanged, EVENTS.accentChanged, EVENTS.themeChanged];

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
    unwatch() { for (const off of offs) off(); },
  };
  const offs = WATCHED.map((channel) => subscribe(channel, () => { stale = true; }));
  return look.restyle();
};
