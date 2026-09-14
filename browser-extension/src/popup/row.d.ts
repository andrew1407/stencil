// Shapes for popup/row.js — one list row: thumbnail, name, badges, pin/actions buttons.
import type { PopupImage } from './model.js';

export interface FilterTransition {
  begin(): string[];
  end(opts?: { skipEnter?: string[] }): unknown;
}

/** Unregisters a leaving ghost from the lazy-measure observer (row.js owns both). */
export declare const filterTransition: FilterTransition;
export declare const renderRow: (image: PopupImage) => void;
