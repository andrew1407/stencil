import { icon } from './icons.js';

// ── The context menu's trailing arrow slot ──────────────────────
// ALWAYS rendered, fixed-width (css/components/contextMenu.css), whether or not the row
// nests a submenu — so a row's hotkey lands on the same right edge either way. This only
// decides whether the chevron paints; the space is reserved regardless.
export const ctxArrow = (has = false) => has ? `<span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>` : '<span class="ctx-arrow"></span>';
