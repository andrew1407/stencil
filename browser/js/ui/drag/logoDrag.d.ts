// Dragging the header logo onto the canvas: a view-only clean preview while over it, the clean
// view committed through each control's own setter when dropped there.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { IconDragHandle, IconDragHooks } from './iconDrag.js';
import type { LogoHold } from '../logo/stageTrigger.js';

/** Filter → none, Show Lines and Show Points off, compare → none, each only when applied. */
export declare const commitCleanView: (app: DrawingApp) => void;

/** The drag's hooks over the canvas region `viewport()`; a modified press, a fired hold or the
 *  accent menu up (`menuUp`) refuses. */
export declare const logoDragHooks: (app: DrawingApp, opts: {
  viewport(): Element | null;
  hold?: LogoHold | null;
  menuUp?(): boolean;
}) => Required<IconDragHooks>;

/** Wires the drag onto the header mark; `hold` is the mark's hold, which a drag drops. */
export declare const wireLogoDrag: (logo: Element | null, app: DrawingApp | null,
  opts?: { hold?: LogoHold | null }) => IconDragHandle | null;
