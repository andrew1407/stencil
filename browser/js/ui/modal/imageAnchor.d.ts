/** A client-rect-shaped anchor box for a modal flight. */
export interface AnchorRect {
  left: number; top: number; right: number; bottom: number; width: number; height: number;
}

/** Side length of the small box a canvas/toolbar flight grows out of or pours into. */
export declare const IMAGE_ANCHOR_PX: number;

/** A small box on the canvas area's centre, falling back to the window's centre. */
export declare const canvasAnchorRect: () => AnchorRect;

/** The visible half of the toolbar's Open Image pair, else the canvas centre. */
export declare const openImageAnchorRect: () => AnchorRect | DOMRect;

/** confirmModal opts: open from the canvas, close into the Open control when an image opens. */
export declare const openImageConfirmAnchors: (opensImage?: (answer: unknown) => boolean) => {
  openAnchor: AnchorRect;
  closeAnchor: (answer: unknown) => AnchorRect | DOMRect;
};
