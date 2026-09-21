// Shape of completion.js — the suggestion list for a caret in a .stc buffer.
import type { ScriptToken } from './parser/script/scriptTypes.js';
export declare const COLOR_WORDS: readonly string[];
export declare const LAYOUT_MODES: readonly string[];
export declare const USE_SUBS: readonly string[];
export declare const templateNames: (tokens: ScriptToken[]) => string[];
export declare const itemsFor: (linePrefix: string, tokens: ScriptToken[]) => unknown[];
export declare const provider: {
  provideCompletionItems: (document: unknown, position: unknown) => Promise<unknown[]>;
};
export declare const register: (context: unknown) => unknown;
