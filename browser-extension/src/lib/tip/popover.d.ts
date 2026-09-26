export interface Rect { left: number; top: number; bottom: number; }
export interface Box { width: number; height: number; }
export interface Viewport { width: number; height: number; }

/** Below the anchor, flipped above when above has more room, clamped inside the viewport. */
export declare function popoverPosition(args: {
  anchor: Rect; box: Box; viewport: Viewport; gap?: number; margin?: number;
}): { left: number; top: number };

export interface ModalOpenGesture {
  click(): void;
  dblclick(): void;
  contextmenu(): void;
  /** True when a shortcut claimed an open peek/linger instead of it toggling. */
  hotkey(): boolean;
  /** `origin` is the opener peeking; a window that holds it stays open. */
  altHover(origin?: Element | null): void;
  altRelease(): void;
  boxEnter(): void;
  boxLeave(): void;
  pressStart(p?: { x?: number; y?: number; touch?: boolean }): void;
  pressMove(p?: { x?: number; y?: number }): void;
  pressEnd(): void;
  notifyClosed(): void;
}

export interface ModalOpenGestureDeps {
  openFull: () => void;
  openPopover: () => void;
  closePopover?: () => void;
  isPopoverOpen?: () => boolean;
  isPeekEngaged?: () => boolean;
  holdLinger?: () => boolean;
  holds?: (el: Element) => boolean;
  delay?: number;
  eagerClick?: boolean;
  holdMs?: number;
  slop?: number;
  setTimer?: typeof setTimeout;
  clearTimer?: typeof clearTimeout;
}

/** The gesture machine behind one modal-opening icon (click / dblclick / right-click / Alt-hover / long press). */
export declare const createModalOpenGesture: (deps: ModalOpenGestureDeps) => ModalOpenGesture;
