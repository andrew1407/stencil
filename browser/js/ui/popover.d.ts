export declare const DOUBLE_CLICK_MS: number;
export declare const LONG_PRESS_MS: number;
export declare const PRESS_SLOP_PX: number;
export declare const LINGER_CLOSE_MS: number;

export interface Rect { left: number; top: number; width: number; height: number; bottom?: number }

/** Below the anchor, flipped above on overflow, clamped inside the viewport. Pure. */
export declare const popoverPosition: (args: {
  anchor: { left: number; top: number; bottom: number };
  box: { width: number; height: number };
  viewport: { width: number; height: number };
  gap?: number;
  margin?: number;
}) => { left: number; top: number };

export interface ModalOpenGesture {
  click(): void;
  dblclick(): void;
  contextmenu(): void;
  /** True when a shortcut claimed an open peek/linger instead of it toggling. */
  hotkey(): boolean;
  altHover(): void;
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
  delay?: number;
  eagerClick?: boolean;
  holdMs?: number;
  slop?: number;
  setTimer?: typeof setTimeout;
  clearTimer?: typeof clearTimeout;
}

/** The gesture machine behind one modal-opening icon (click / dblclick / right-click / Alt-hover / long press). */
export declare const createModalOpenGesture: (deps: ModalOpenGestureDeps) => ModalOpenGesture;

/** DOM wiring for createModalOpenGesture over a button. */
export declare const wireModalOpenGestures: (btn: HTMLElement, deps: ModalOpenGestureDeps) => ModalOpenGesture;
