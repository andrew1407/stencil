/** The Open-Image picture's Sliding pour / Transition grow out of (`opening`) or into `from`; ms, or 0. */
export declare const flyMedia: (
  el: HTMLImageElement | HTMLVideoElement,
  from: Element | null,
  opening: boolean,
  done?: (() => void) | null,
) => number;

/** Ends a flight `flyMedia` started on `el`, leaving it shown. */
export declare const cancelMediaFlight: (el: HTMLImageElement | HTMLVideoElement | null) => void;
