// Desktop mouse interaction wiring: Alt/middle pan, Shift+drag zoom-rect, rect-draw
// sweep, compare-divider drag, and point/segment/whole-line drag. Holds only the pan
// cursor delta; every drag-state field lives on the app, shared with inputController.js.
import type { DrawingApp } from '../drawingApp.js';

export declare class PointerController {
  app: DrawingApp;
  constructor(app: DrawingApp);
  /** Listens on the canvas, the viewport and the document for the whole session. */
  wirePanDrag(): void;
}
