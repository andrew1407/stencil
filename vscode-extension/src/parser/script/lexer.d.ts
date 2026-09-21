// Shape of lexer.js — port of core/script/lexer.cpp.
import type { ScriptDiagnostic, ScriptToken } from './types.js';
export const isHexColorWord: (word: string) => boolean;
export const lexScript: (text: string) => { tokens: ScriptToken[]; diagnostics: ScriptDiagnostic[] };
