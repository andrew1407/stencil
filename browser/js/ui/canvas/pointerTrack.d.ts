/** Window-level pointermove/up/cancel tracking for one drag gesture, until release. */
export declare const trackPointer: (onMove: (e: PointerEvent) => void, onUp: (e: PointerEvent) => void) => void;

/** Wire the strip above a composer to resize `input` by dragging its top edge. */
export declare const wireInputSizer: (
  sizer: HTMLElement, input: HTMLElement,
  opts?: { host?: HTMLElement; onDrag?: () => void; hold?: (on: boolean) => void },
) => void;
