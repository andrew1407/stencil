// ── Shared UI motion helpers (mirror of browser/js/ui/motion.js) ────────────
// Pure decoration: with no IntersectionObserver/MutationObserver nothing runs and lists
// show normally. CSS owns the keyframes (lib/animations/); this only toggles classes.
// The sections live one per file under motion/; this is the single import point.
//
// The two gates every helper asks: `motionReduced()` is "nothing may move", `dustEnabled()`
// is "and it may be made of particles" — false in 'slide', where each surface keeps its own
// plain CSS entrance. Re-exported so a caller needs one import, not two.
export { prefersReducedMotion, motionMode, motionReduced, dustEnabled, particleStyle } from './motionPrefs.js';
export * from './motion/reveal.js';
export * from './motion/tiles.js';
export * from './motion/painters.js';
export * from './motion/surfaceMotion.js';
export * from './motion/disintegrate.js';
export * from './motion/enterLeave.js';
export * from './motion/chatFx.js';
export * from './motion/surfaces.js';
export * from './motion/tips.js';
