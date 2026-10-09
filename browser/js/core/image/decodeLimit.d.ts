// The decoded-pixel cap every load holds a picture to before it is drawn (LIMITS.imageMaxSide and
// imageMaxPixels, the CLI decoder's STBI_MAX_DIMENSIONS), and the 2D context a canvas the
// browser refuses to allocate fails with by name, not as a TypeError mid-load.

/** The notice a refused picture gets. */
export declare const IMAGE_TOO_LARGE: string;
/** The error a canvas whose getContext answered null throws. */
export declare const CANVAS_UNAVAILABLE: string;
/** Either side past imageMaxSide, or the area past imageMaxPixels (px). */
export declare const imageTooLarge: (width: number, height: number) => boolean;
/** The canvas's 2D context; throws CANVAS_UNAVAILABLE where getContext answers null (a canvas past the browser's limit). */
export declare const context2d: (canvas: HTMLCanvasElement) => CanvasRenderingContext2D;
