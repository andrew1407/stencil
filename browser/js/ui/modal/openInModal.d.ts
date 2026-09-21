import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** Mirror the current session into the Desktop app or the Telegram bot. */
export declare class StencilOpenInModal extends StencilElement {
  static inner(): string;
  static template(): string;
  /** Open for a projects-list row rather than the live session; `anchors` is the flight's ends. */
  openFor(id: string | number | null, anchors?: { from?: unknown; backTo?: unknown }): void;
  wire(app: DrawingApp): { open: (from?: unknown, backTo?: unknown, opts?: unknown) => void; close: () => void };
}
