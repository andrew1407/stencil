/** Px width of the hit-test strip along a scrollable's edge. */
export declare const SCROLLBAR_STRIP_PX: number;

/** Whether (x, y) is on `box`'s scrollbar strip (right edge if canY, bottom edge if canX). */
export declare const scrollbarHit: (
  box: { left: number; right: number; top: number; bottom: number },
  x: number, y: number,
  opts?: { canX?: boolean; canY?: boolean; strip?: number },
) => boolean;

/** The scrolling ancestor of `target` whose own native scrollbar is under (x, y), or null. */
export declare const scrollbarOwnerAt: (target: Element, x: number, y: number) => Element | null;

/** Marks the scrolling element under the pointer with `sb-hover` while over ITS scrollbar. */
export declare const wireScrollbarHover: (root?: Document) => void;
