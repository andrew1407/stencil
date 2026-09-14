// Shapes for options/pinsDom.js — shared by pins.js (the render pass) and pinRow.js
// (one row): the DOM handles, and the two keys a pin/connection row is matched by.

/** One entry of the pinned-images store (lib/pins.js). */
export interface Pin {
  source: string;
  site: string;
  resource?: string;
  name?: string;
  kind?: string;
  t?: number;
  color?: string;
  keywords?: string[];
}

/** What createFilterTransition (lib/motion.js) hands a wholesale-rebuilding list. */
export interface FilterTransition {
  begin(): Array<string | undefined>;
  end(opts?: { skipEnter?: string[] }): unknown;
  clear(): void;
}

export declare const siteSel: HTMLSelectElement;
export declare const pinListEl: HTMLUListElement;
export declare const pinEmptyEl: HTMLElement;
export declare const pinClearBtn: HTMLButtonElement;
export declare const pinSearchEl: HTMLInputElement | null;
export declare const pinSearchModeEl: HTMLSelectElement | null;
export declare const hostLabel: (origin: string) => string;
/** The pair a pin is identified by in storage. */
export declare const pinKey: (pin: Pin) => string;
export declare const pinTransition: FilterTransition;
/** Moves a row's disintegrate-dust host to <body> so a list rebuild can't take it with it. */
export declare const liftDust: (el: Element) => void;
/** Re-fetches a hotlink-protected thumbnail through the extension's host permissions. */
export declare const recoverThumb: (img: HTMLImageElement, source: string, kind: string, resource?: string) => void;
