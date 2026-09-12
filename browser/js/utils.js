// One import point over ./utils/ (a file per concern); the mutable hotkey registry lives
// in ./core/hotkeys.js.
export { onWindowResize, perFrame } from './ui/frameSync.js';
// Lives in ui/scrollbarHover.js (an extension port); re-exported for its old callers.
export { SCROLLBAR_STRIP_PX, scrollbarHit, scrollbarOwnerAt, wireScrollbarHover } from './ui/scrollbarHover.js';
export * from './utils/dom.js';
export * from './utils/math.js';
export * from './utils/appQueries.js';
export * from './utils/nameEditor.js';
export * from './utils/panelResizer.js';
export * from './utils/geometry.js';
export * from './utils/color.js';
export * from './utils/keys.js';
