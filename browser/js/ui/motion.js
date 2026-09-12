// Shared UI motion helpers: pure decoration, CSS owns the keyframes (css/animations/). The
// sections live one per file under motion/; this is the single import point.
// `motionReduced()` is "nothing may move", `dustEnabled()` is "and it may be particles".
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
