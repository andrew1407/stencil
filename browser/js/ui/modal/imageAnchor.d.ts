/** A client-rect-shaped anchor box for a modal flight. */
export interface AnchorRect {
  left: number; top: number; right: number; bottom: number; width: number; height: number;
}

/** Side length of the small box a canvas/toolbar flight grows out of or pours into. */
export declare const IMAGE_ANCHOR_PX: number;

/** True while `el` sits in a toolbar that is out of sight: rows folded, or the fullscreen strip hidden. */
export declare const toolbarAway: (el: unknown) => boolean;
/** An 8x2 strip of the window's top edge over `el`'s centre, in client px. */
export declare const edgeAbove: (el: Element) =>
  { left: number; top: number; width: number; height: number; right: number; bottom: number };

/** An 8x2 strip of the window's bottom edge under `el`'s centre, in client px. */
export declare const edgeBelow: (el: Element) =>
  { left: number; top: number; width: number; height: number; right: number; bottom: number };
/** A small box on the canvas area's centre, falling back to the window's centre. */
export declare const canvasAnchorRect: () => AnchorRect;

/** The half of the toolbar's Open Image pair the OUTCOME leaves showing, else the canvas centre. */
export declare const openImageAnchorRect: (imageOpen?: boolean) => AnchorRect | DOMRect;

/** Where a toolbar control sits once the editor is empty. */
export declare const emptiedControlRect: (id: string) => AnchorRect | DOMRect | null;

/** confirmModal opts: open from the canvas, close into the Open control when an image opens. */
export declare const openImageConfirmAnchors: (opensImage?: (answer: unknown) => boolean) => {
  openAnchor: AnchorRect;
  closeAnchor: (answer: unknown) => AnchorRect | DOMRect;
};
/** Re-aims the open dropdown lists whose trigger sits in `strip` at the top edge above it, for their close. */
export declare const aimListsUp: (strip: Element | null | undefined) => void;
/** Over another window (`stacked`) none, so the question grows from the press; else openImageConfirmAnchors. */
export declare const openImageConfirmAnchorsOver: (stacked: boolean, opensImage?: (answer: unknown) => boolean) =>
  { openAnchor?: unknown; closeAnchor?: (answer: unknown) => unknown };
