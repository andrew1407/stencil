// Canvas hover + double-click delete: everything a pointer move decides — the cursor, the
// hover ring, the coordinate readout/tooltip and the Lines-row tint. canvasMouseMove is
// bound through a one-per-frame rAF coalescer (ui/bindings/canvasPointer.js).
import type { DrawingApp } from '../drawingApp.js';

type PointerLike = Pick<MouseEvent, 'clientX' | 'clientY' | 'altKey' | 'shiftKey' | 'ctrlKey' | 'metaKey'>;

export declare const canvasMouseMove: (app: DrawingApp, e: PointerLike & Pick<MouseEvent, 'timeStamp'>) => void;
/** Deletes the line under the cursor (not while drawing, in compare view, or with Alt). */
export declare const canvasDblClick: (app: DrawingApp, e: PointerLike) => void;
