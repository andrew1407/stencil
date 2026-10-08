import type { DrawingApp } from '../../../core/drawingApp.js';

export interface DragClose {
  /** A row was picked up: only the project open in this tab arms the ✕. */
  begin(held: { meta: Record<string, unknown>; isRemote: boolean } | null | undefined): void;
  /** True while the armed ✕ is under the pointer. */
  track(x: number, y: number): boolean;
  /** True when the drop landed on the armed ✕: the project closes here, unasked. */
  drop(x: number, y: number): boolean;
  end(): void;
}

/** The projects window's ✕ as the drop that closes the open project here. */
export declare function createDragClose(deps: {
  app: DrawingApp;
  closeBtn: Element | null;
  settle?: () => void;
}): DragClose;
