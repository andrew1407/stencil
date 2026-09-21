export interface TouchDragOptions {
  /** (e) => bool — false ignores this pointerdown (an interactive child). */
  canStart?: (e: PointerEvent) => boolean;
  /** Drag began, after the long-press. */
  onStart?: () => void;
  onMove?: (clientX: number, clientY: number) => void;
  onDrop?: (clientX: number, clientY: number) => void;
  /** pointercancel while dragging. */
  onCancel?: () => void;
  longPressMs?: number;
  moveCancelPx?: number;
}

/** Pointer-based long-press drag for touch/pen on the reorderable modal lists. */
export declare function makeTouchDraggable(row: HTMLElement, opts: TouchDragOptions): void;
