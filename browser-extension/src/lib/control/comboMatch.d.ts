// PORT of browser/js/ui/control/comboMatch.d.ts (browser-extension/tests/portParity.test.js).

export interface Combo { mods: Set<string>; key: string; }
export declare const parseCombo: (combo: string) => Combo;
/** Both the typed `key` and the physical `code`: on a Mac Alt+A reports "å". */
export interface EventCombo { mods: Set<string>; keys: string[]; }
export declare const eventCombo: (e: KeyboardEvent) => EventCombo;
export declare const comboMatchesEvent: (combo: string, e: KeyboardEvent) => boolean;
