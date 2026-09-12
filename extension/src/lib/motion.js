// The single import point for the motion layer (mirror of browser/js/ui/motion.js); the
// sections live one per file under motion/. CSS owns the keyframes — this only toggles classes.
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
