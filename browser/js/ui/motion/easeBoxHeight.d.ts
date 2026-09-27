/** The desktop dialog's height-ease duration (openImageDialogParts.hpp OI_RESIZE_MS). */
export declare const BOX_RESIZE_MS: number;
/** The easing every box height flight takes. */
export declare const BOX_RESIZE_EASE: string;

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

/**
 * Let a list held at `held` px through a removal ease down to its natural height instead of
 * dropping there in one frame. No-op when it needs no less, or under reduced motion. Returns a cancel.
 */
export declare const releaseHeldHeight: (el: HTMLElement, held: number, ms?: number) => (() => void);

/** The onOpen/onClose wiring both modal windows share (start after the entrance). */
export declare const modalBoxEase: (overlay: Element) => { start: () => void; stop: () => void };
