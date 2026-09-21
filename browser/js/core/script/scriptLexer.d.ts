// Shape of scriptLexer.js — port of core/script/scriptLexer.cpp.
import type { ScriptDiagnostic, ScriptToken } from './scriptTypes.js';
export const isHexColorWord: (word: string) => boolean;
export const lexScript: (text: string) => { tokens: ScriptToken[]; diagnostics: ScriptDiagnostic[] };
