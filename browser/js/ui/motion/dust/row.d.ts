/** The keyword-chip dust spec every row here flies on (tiles.js CHIP_*). */
export declare const CHIP_DUST: Readonly<Record<string, unknown>>;

/** `(show, animate)` — a row that slides its own height open/shut under its cloud. */
export declare const makeDustRow: (
  el: HTMLElement,
  display?: string,
  dustEl?: () => HTMLElement,
) => (show: boolean, animate?: boolean) => void;

/** `(show, animate)` — the same cloud for an inline control, with no height to collapse. */
export declare const makeDustToggle: (
  el: HTMLElement,
  display?: string,
) => (show: boolean, animate?: boolean) => void;

/** Sliding / Transition's own flight of `el` in or out (no cloud); null in any other mode. */
export declare const modeFlight: (el: HTMLElement, show: boolean, ms?: number) => Animation | null;

/** Drops the flight `modeFlight` left holding on `el`. */
export declare const cancelModeFlight: (el: HTMLElement) => void;
