// Shapes for popup/row/row.js — one list row: thumbnail, name, badges, pin/actions buttons.
import type { PopupImage } from '../list/model.js';

/** `image`'s row as a detached <li> keyed by data-key; popup/list/rows.js places it. */
export declare const buildRow: (image: PopupImage) => HTMLLIElement;
