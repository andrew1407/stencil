/** The desktop dialog's height-ease duration (openImageDialogParts.hpp OI_RESIZE_MS). */
export declare const BOX_RESIZE_MS: number;

/**
 * Ease a content-sized box between its natural heights instead of snapping.
 * `scroller` is its scrolling body, which the wanted height is measured from.
 * Desktop twin: OpenImageDialog::animateHeightTo. No-op under reduced motion or
 * without ResizeObserver/Web Animations. Returns a stop function.
 */
export declare const easeBoxHeight: (
  box: HTMLElement | null,
  scroller: HTMLElement | null,
  ms?: number,
) => (() => void);

/** The onOpen/onClose wiring both modal windows share (start after the entrance). */
export declare const modalBoxEase: (overlay: Element) => { start: () => void; stop: () => void };
