export declare const DOUBLE_CLICK_MS: number;
export declare const LONG_PRESS_MS: number;
export declare const PRESS_SLOP_PX: number;
export declare const LINGER_CLOSE_MS: number;

export interface Rect { left: number; top: number; width: number; height: number; bottom?: number }

/** Below the anchor, flipped above when above has more room, clamped inside the viewport. Pure. */
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
  /** `origin` is the opener peeking; a window that holds it stays open. */
  altHover(origin?: Element | null): void;
  altRelease(): void;
  boxEnter(): void;
  boxLeave(): void;
  pressStart(p?: { x?: number; y?: number; touch?: boolean }): void;
  pressMove(p?: { x?: number; y?: number }): void;
  pressEnd(): void;
  /** A drag took the press: a pending click, the long press and a peek's close are dropped. */
  dragged(): void;
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

/** DOM wiring for createModalOpenGesture over a button. */
export declare const wireModalOpenGestures: (btn: HTMLElement, deps: ModalOpenGestureDeps) => ModalOpenGesture;
