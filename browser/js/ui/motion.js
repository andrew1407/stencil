// Shared UI motion helpers: pure decoration, CSS owns the keyframes (css/animations/). The
// sections live one per file under motion/; this is the single import point.
// `motionReduced()` is "nothing may move", `dustEnabled()` is "and it may be particles".
export { dustEnabled, motionReduced } from './motion/motionPrefs.js';
export * from './motion/reveal.js';
export * from './motion/flip.js';
export * from './motion/surface/themeSwap.js';
export * from './motion/dust/swapDust.js';
export * from './motion/surface/themeSwapPlay.js';
export * from './motion/enterLeave.js';
export * from './motion/control/fx.js';
export * from './motion/surface/tiles.js';
export * from './motion/disintegrate.js';
export * from './motion/surface/motion.js';
export * from './motion/surface/painters.js';
export * from './motion/surface/surfaces.js';
export * from './motion/control/tips.js';
export * from './motion/surface/marks.js';
export * from './motion/control/revealControls.js';
export * from './motion/control/chatFx.js';
export * from './motion/dust/canvasDustGrid.js';
export * from './motion/dust/canvasDustStage.js';
export * from './motion/dust/canvasDustDraw.js';
export * from './motion/control/canvasFx.js';
export * from './motion/strokeFly.js';
