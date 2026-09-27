// Shape of lexer.js — port of core/script/lexer.cpp.
import type { ScriptDiagnostic, ScriptToken, TokenKind } from './types.js';
export const isHexColorWord: (word: string) => boolean;
export const classifyWord: (word: string) => { kind: TokenKind; unitAt: number };
export const lexScript: (text: string) => { tokens: ScriptToken[]; diagnostics: ScriptDiagnostic[] };
