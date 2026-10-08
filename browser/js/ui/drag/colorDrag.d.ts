import type { DrawingApp } from '../../core/drawingApp.js';
import type { IconDragHooks } from './iconDrag.js';
import type { ColorSwatch } from './colorDragSwatches.js';

export declare const CHIP_CLASS: string;
export declare const CHIP_OFFSET_PX: number;

export interface ColorChip {
  move(x: number, y: number): void;
  destroy(): void;
}

/** The drag hooks of the swatch at `el`; `chipFor` and `targets` are the DOM seams a test replaces. */
export declare const colorDragHooks: (el: Element, seams?: {
  chipFor?(color: string): ColorChip;
  targets?(): ColorSwatch[];
}) => Required<Pick<IconDragHooks, 'start' | 'move' | 'drop' | 'cancel'>>;

/** One capture listener on `root`: a swatch's first press wires it as a drag source. */
export declare const wireColorDrag: (app: DrawingApp, root?: Document | HTMLElement) => void;
