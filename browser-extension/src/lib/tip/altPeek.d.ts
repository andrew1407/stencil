import type { ModalOpenGesture } from './popover.js';

export interface AltPeekDeps {
  open: () => void;
  close: () => void;
  isOpen?: () => boolean;
  enabled?: () => boolean;
  isTyping?: () => boolean;
}

/** Starts following the pointer for pointerIn (once per page). */
export declare const trackPointer: () => void;

/** The pointer is over `el`: its :hover, or a hit test at the last pointer position. */
export declare const pointerIn: (el: Element | null | undefined) => boolean;

/** One Alt trigger on the shared document keydown/keyup + window blur listeners; `isIn` hit-tests
 *  the pointer once per press for every trigger. */
export declare const onAltKeys: (entry: {
  press?: (e: KeyboardEvent, isIn: (el: Element | null | undefined) => boolean) => void;
  /** `e` is the Alt keyup; a window blur releases with none. */
  release?: (e?: KeyboardEvent) => void;
}) => void;

/** Pointer inside `box`, or inside a list one of its dropdowns opened on <body>. */
export declare const peekEngaged: (box: Element | null | undefined) => boolean;

/** Wires a window's boxEnter/boxLeave; its own dropdowns' lists count as inside it. */
export declare const wirePeekBox: (box: HTMLElement, g: ModalOpenGesture) => void;

/** An Alt peek released on one of `menu`'s `rows` (a selector) clicks it; `picking()` is true during that click. */
export declare const wireReleasePick: (menu: HTMLElement | null, g: ModalOpenGesture, rows: string,
                                       deps: { isPeek: () => boolean; isShowing: () => boolean }) => { picking: () => boolean };

/** Alt+hover on `hover` peeks `menu`; Alt released over the list lingers until the pointer leaves it. */
export declare const wireAltPeek: (hover: HTMLElement, menu: HTMLElement, deps: AltPeekDeps) => ModalOpenGesture;
