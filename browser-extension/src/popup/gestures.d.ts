// Shapes for popup/gestures.js — click/double-click disambiguation and drag-out for a row.
import type { PopupImage } from './model.js';

export declare const bindRowGestures: (el: Element, image: PopupImage) => void;
export declare const bindRowDrag: (row: Element, image: PopupImage) => void;
/** The row a dragstart captured — a `dragover` exposes no data, only its types. */
export declare const getDraggingRow: () => PopupImage | null;
