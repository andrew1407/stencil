// Shape of decorations.js — explicit per-family colours drawn over the themed tokens.
import type { ScriptToken } from './parser/script/scriptTypes.js';
export declare const DEBOUNCE_MS: number;
export declare const rangesFor: (tokens: ScriptToken[], types: string[]) => Map<string, unknown[]>;
export declare const register: (context: unknown) => {
  paintAll: () => void;
  typesFor: () => Map<string, unknown>;
};
