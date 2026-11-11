// A cheat code's look made the user's own settings: the skin's motion and accent are stored, so a
// relaunch keeps them until the user picks others. Off in the same session gives back what they
// replaced, unless the user moved it meanwhile. Desktop twin: ThemePainter::adoptSkinLook.
import { motionPrefs, setMotionPrefs } from '../motion/motionPrefs.js';

const sameMotion = (want) => {
  const now = motionPrefs();
  return Object.entries(want).every(([k, v]) => now[k] === v);
};
// Each switch the skin set and the user left alone, back to what it replaced.
const motionBack = (look, before) => {
  const now = motionPrefs();
  return Object.fromEntries(Object.keys(look).filter((k) => now[k] === look[k] && before[k] !== look[k]).map((k) => [k, before[k]]));
};

// `look`: { motion: Partial<MotionPrefs>, accent: preset key }. Returns the give-back for off.
export const adoptLook = (app, look) => {
  const was = motionPrefs();
  const before = {
    motion: Object.fromEntries(Object.keys(look.motion).map((k) => [k, was[k]])),
    accent: app.accent, custom: app.customAccent || null,
  };
  if (!sameMotion(look.motion)) setMotionPrefs(look.motion);
  if (before.custom || before.accent !== look.accent) app.accents?.setAccent?.(look.accent);
  return () => {
    const back = motionBack(look.motion, before.motion);
    if (Object.keys(back).length) setMotionPrefs(back);
    if (app.customAccent || app.accent !== look.accent) return;   // the user's own pick stands
    if (before.custom) app.accents?.setCustomAccent?.(before.custom);
    else if (before.accent !== look.accent) app.accents?.setAccent?.(before.accent);
  };
};
