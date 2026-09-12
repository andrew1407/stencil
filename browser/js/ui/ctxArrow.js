import { icon } from './icons.js';

// The trailing arrow slot, always rendered fixed-width (css/components/contextMenu.css)
// so a row's hotkey lands on the same right edge whether or not it nests a submenu.
export const ctxArrow = (has = false) => has ? `<span class="ctx-arrow">${icon('chevron-right', { size: 12 })}</span>` : '<span class="ctx-arrow"></span>';
