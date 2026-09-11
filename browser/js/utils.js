// ── Utilities (consolidated): DOM, mount, notify, geometry, color, hotkeys ──
// One import point over ./utils/, which holds a file per concern. The mutable hotkey
// registry lives in ./core/hotkeys.js (the `hotkeys` singleton); the pure parse/match
// helpers are in ./utils/keys.js.
export { onWindowResize, perFrame } from './ui/frameSync.js';
// Lives in ui/scrollbarHover.js (the extension ports that file); re-exported here for
// the callers that always found it in utils.
export { SCROLLBAR_STRIP_PX, scrollbarHit, scrollbarOwnerAt, wireScrollbarHover } from './ui/scrollbarHover.js';
export * from './utils/dom.js';
export * from './utils/math.js';
export * from './utils/appQueries.js';
export * from './utils/nameEditor.js';
export * from './utils/panelResizer.js';
export * from './utils/geometry.js';
export * from './utils/color.js';
export * from './utils/keys.js';
