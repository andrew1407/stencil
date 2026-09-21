// Shape of semanticTokens.js — classified parser tokens as VS Code semantic tokens.
import type { ScriptToken } from './parser/script/scriptTypes.js';
export declare const TOKEN_TYPES: readonly string[];
export declare const TYPE_INDEX: Record<string, number>;
export declare const LEGEND: unknown;
export declare const tokenRows: (tokens: ScriptToken[]) => [number, number, number, number][];
export declare const provider: {
  provideDocumentSemanticTokens: (document: unknown) => Promise<unknown>;
};
export declare const register: (context: unknown) => unknown;
