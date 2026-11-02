// Shapes for popup/list/rows.js — the image list's rows, windowed (lib/virtualList/rowWindow.js) once long.
import type { PopupImage } from './model.js';

export interface FilterTransition {
  begin(): string[];
  end(opts?: { skipEnter?: string[] }): unknown;
}

/** Diffs a rebuild by row key; a leaving ghost is unobserved and loses its window index. */
export declare const filterTransition: FilterTransition;
/** Lists state.filtered: the window when it is long, else every row. */
export declare const showRows: () => void;
/** Empties the list and resets the window. */
export declare const clearRows: () => void;
/** `image`'s row, rendered and scrolled to first when the window holds another span; null when unlisted. */
export declare const revealRow: (image: PopupImage) => Element | null;
