// Shapes for ui/comboMatch.js — a written shortcut and a KeyboardEvent reduced to the same
// shape, so the keycaps a tooltip drew can be matched against what was pressed. The module is
// byte-pinned with browser-extension/src/lib/comboMatch.js (browser-extension/tests/portParity.test.js).

export interface Combo { mods: Set<string>; key: string; }
export declare const parseCombo: (combo: string) => Combo;
/** Both the typed `key` and the physical `code`: on a Mac Alt+A reports "å". */
export interface EventCombo { mods: Set<string>; keys: string[]; }
export declare const eventCombo: (e: KeyboardEvent) => EventCombo;
export declare const comboMatchesEvent: (combo: string, e: KeyboardEvent) => boolean;
