// Shape of semanticTokens.js — lexer TokenKinds as VS Code semantic tokens.
import type { ScriptToken } from './parser/scriptTypes.js';
export declare const TOKEN_TYPES: string[];
export declare const KIND_TYPE: Record<string, string>;
export declare const LEGEND: unknown;
export declare const tokenRows: (tokens: ScriptToken[]) => [number, number, number, number][];
export declare const provider: {
  provideDocumentSemanticTokens: (document: unknown) => Promise<unknown>;
};
export declare const register: (context: unknown) => unknown;
