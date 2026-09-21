import type { DrawingApp } from '../../core/drawingApp.js';
import type { StencilElement } from '../base.js';

/** The hover/coordinate tooltip: owns its dynamically-filled DOM and show/hide/position logic. */
export declare class StencilTooltip extends StencilElement {
  app: DrawingApp | null;
  showTimer: ReturnType<typeof setTimeout> | null;
  pendingKey: string | null;
  pendingReveal: (() => void) | null;
  shownKey: string | null;
  static readonly SHOW_DELAY_MS: number;
  static readonly IN_MS: number;
  static readonly OUT_MS: number;

  static inner(): string;
  static template(): string;
  wire(app: DrawingApp): void;
  applyHover(clientX: number, clientY: number, x: number, y: number, mods: { altKey?: boolean; ctrlKey?: boolean; metaKey?: boolean; shiftKey?: boolean }, immediate?: boolean): void;
  refresh(mods: { altKey?: boolean; ctrlKey?: boolean; metaKey?: boolean; shiftKey?: boolean }): void;
  scheduleShow(key: string, revealFn: () => void, immediate: boolean): void;
  show(clientX: number, clientY: number, x: number, y: number): void;
  showLine(clientX: number, clientY: number, line: { points: { x: number; y: number }[] }, showAll: boolean): void;
  dust(clientX: number, clientY: number, enter: boolean): void;
  reveal(clientX: number, clientY: number): void;
  position(clientX: number, clientY: number): void;
  hide(): void;
}
