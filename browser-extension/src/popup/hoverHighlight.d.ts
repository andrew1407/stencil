// Shapes for popup/hoverHighlight.js — the two-way row↔page hover outline.
import type { PopupImage } from './model.js';

export declare const runHoverHighlight: (source: string, rowTabId?: number | null) => Promise<void>;
export declare const bindHoverHighlight: (rowEl: Element, image: PopupImage) => void;
/** The page reported `source` under the cursor; highlights the matching row, if any. */
export declare const highlightListRowForSource: (source: string) => void;
