// The points panel's fold into (and out of) its rail — its dust, veil and slide.
export declare const VEILED_CLASS: 'coord-veiled';
export declare const PANEL_DUST_IN_MS: number;
export declare const PANEL_DUST_OUT_MS: number;
export declare const PANEL_SLIDE_IN_MS: number;
export declare const PANEL_SLIDE_OUT_MS: number;
export declare const PANEL_FADE_MS: number;
export declare const PANEL_CLOCK_VARS: Readonly<Record<string, number>>;
export declare function createCoordFold(panel: HTMLElement, header: HTMLElement,
  body: HTMLElement): (hidden: boolean) => void;
