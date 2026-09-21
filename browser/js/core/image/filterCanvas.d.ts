export declare class ImageFilterCanvas {
  /** Offscreen canvas of `image` with 'contour' | 'custom' applied; cached on (image, filter, color). */
  canvasFor(image: CanvasImageSource & { width: number; height: number },
            filter: 'contour' | 'custom', color: string | null): HTMLCanvasElement | OffscreenCanvas;
}
