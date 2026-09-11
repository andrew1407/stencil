// ── Shared UI motion helpers ────────────────────────────────────
// Pure decoration: a missing IntersectionObserver/MutationObserver (node tests,
// old engines) simply means no animation — never a broken or hidden view. CSS
// owns the actual keyframes (css/animations.css); this file only toggles classes.
// The sections live one per file under motion/; this is the single import point.
//
// The two gates every helper asks: `motionReduced()` is "nothing may move"
// (the OS preference, or the user's own 'none'), `dustEnabled()` is "and it may be made
// of particles" — false in 'slide', where each surface keeps its own plain CSS entrance.
// Re-exported so a caller needs one import, not two.
export { dustEnabled, motionReduced } from './motionPrefs.js';
export * from './motion/reveal.js';
export * from './motion/flip.js';
export * from './motion/themeSwap.js';
export * from './motion/swapDust.js';
export * from './motion/themeSwapPlay.js';
export * from './motion/enterLeave.js';
export * from './motion/controlFx.js';
export * from './motion/tiles.js';
export * from './motion/disintegrate.js';
export * from './motion/surfaceMotion.js';
export * from './motion/painters.js';
export * from './motion/surfaces.js';
export * from './motion/tips.js';
export * from './motion/marks.js';
export * from './motion/revealControls.js';
export * from './motion/chatFx.js';
export * from './motion/canvasDustGrid.js';
export * from './motion/canvasDustStage.js';
export * from './motion/canvasDustDraw.js';
export * from './motion/canvasFx.js';
export * from './motion/strokeFly.js';
