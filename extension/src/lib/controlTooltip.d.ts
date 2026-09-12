// PORT of browser/js/ui/controlTooltip.js (extension/tests/portParity.test.js).
export declare const DUST_CURSOR_PX: number;
export interface Point { x: number; y: number; }
export declare const dustOrigin: (centre: Point | null, cursor: Point | null, maxPx?: number) => Point | null;

export interface Combo { mods: Set<string>; key: string; }
export declare const parseCombo: (combo: string) => Combo;
export interface EventCombo { mods: Set<string>; keys: string[]; }
export declare const eventCombo: (e: KeyboardEvent) => EventCombo;
export declare const comboMatchesEvent: (combo: string, e: KeyboardEvent) => boolean;

export declare const dismissTip: () => void;
export declare const initTooltips: () => void;
