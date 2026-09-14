export interface RectLike { left: number; right: number; top: number; bottom: number; }

export declare const SCROLLBAR_STRIP_PX: number;
export declare function scrollbarHit(
  box: RectLike, x: number, y: number, opts?: { canX?: boolean; canY?: boolean; strip?: number },
): boolean;
export declare function scrollbarOwnerAt(target: Element, x: number, y: number): Element | null;
export declare function wireScrollbarHover(root?: Document): void;
